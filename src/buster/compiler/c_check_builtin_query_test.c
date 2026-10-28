// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's source/runtime constant and object-size query actions. Include after c_check_builtin_query.c and c_checker_test.c.
// Fixtures state the expected constants and producer metadata independently; no source object is dereferenced.
// Guarded batches compare individual actions and legal partitions. Original C source assertions and constraints
// additionally run through the existing external Clang subprocess helper, without claiming source integration here.

#define BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH 17u

typedef struct CheckBuiltinQueryTest CheckBuiltinQueryTest;
struct CheckBuiltinQueryTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_query_test_expect(CheckBuiltinQueryTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtins-query: %s\n", description);
        }
    }
}

static bool check_builtin_query_test_equal(const CheckBuiltinQueryResult* a, const CheckBuiltinQueryResult* b)
{
    bool equal = check_test_action_equal(&a->action, &b->action) && check_test_value_equal(&a->runtime_value, &b->runtime_value) && a->evaluated_operands == b->evaluated_operands &&
        a->warnings == b->warnings && a->warning_operands == b->warning_operands && a->mode == b->mode && a->known == b->known && a->constraint == b->constraint &&
        a->source_proof == b->source_proof && a->runtime_plan == b->runtime_plan && a->unsupported == b->unsupported &&
        a->runtime_effects.flags == b->runtime_effects.flags && a->runtime_effects.fault == b->runtime_effects.fault &&
        a->runtime_effects.fault_node == b->runtime_effects.fault_node && a->runtime_effects.reads_fp_environment == b->runtime_effects.reads_fp_environment &&
        a->runtime_effects.may_raise_fp_exceptions == b->runtime_effects.may_raise_fp_exceptions;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        equal &= a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return equal;
}

static bool check_builtin_query_test_constant(const CheckBuiltinQueryResult* result, TypeId type, uint64_t expected)
{
    const CheckValue value = result->action.value;
    const bool equal = result->action.diagnostic == CHECK_DIAG_NONE && value.type == type && value.category == CHECK_RVALUE &&
        value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == expected && (value.flags & BUSTER_CHECK_ICE) != 0 &&
        (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && value.fault == CHECK_FAULT_NONE && value.fault_node == 0 && result->evaluated_operands == 0;
    return equal;
}

static bool check_builtin_query_test_deferred(const CheckBuiltinQueryResult* result, uint64_t expected)
{
    const CheckValue value = result->action.value;
    const CheckValue runtime = result->runtime_value;
    const bool equal = result->action.diagnostic == CHECK_DIAG_NONE && value.type == BUSTER_TYPE_ULONG && value.category == CHECK_RVALUE &&
        value.constant_kind == CHECK_CONSTANT_NONE && value.flags == BUSTER_CHECK_ICE_UNEVALUATED && value.fault == CHECK_FAULT_NONE &&
        runtime.type == BUSTER_TYPE_ULONG && runtime.category == CHECK_RVALUE && runtime.constant_kind == CHECK_CONSTANT_INTEGER &&
        runtime.integer == expected && runtime.flags == 0 && runtime.fault == CHECK_FAULT_NONE && result->evaluated_operands == 0;
    return equal;
}

static CheckValue check_builtin_query_test_pointer(TypeId type, uint32_t object, int64_t offset)
{
    const CheckValue result =
    {
        .type = type,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = object,
        .offset = offset,
    };
    return result;
}

static CheckBuiltinQueryResult check_builtin_query_test_action(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinQueryAction input =
    {
        .id = id,
        .arguments = arguments,
        .argument_count = count,
        .node = 148,
        .source_position = UINT32_MAX,
    };
    CheckBuiltinQueryResult result;
    check_builtin_query_one(table, &input, &result);
    return result;
}

static void check_builtin_query_test_values(CheckBuiltinQueryTest* test, TypeTable* table)
{
    CheckValue argument = check_test_integer(BUSTER_TYPE_INT, 7);
    CheckBuiltinQueryResult result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1) && result.known, "constant_p integer is clean int ICE one");
    argument.flags = 0;
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "constant_p asks foldability rather than original ICE");
    argument = check_test_float(BUSTER_TYPE_DOUBLE, 1.25L);
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "constant_p floating constant");
    const TypeId complex_type = type_complex(table, BUSTER_TYPE_DOUBLE);
    argument = (CheckValue){ .type = complex_type, .category = CHECK_RVALUE };
    CheckBuiltinQueryAction complex_query =
    {
        .id = CBUILTIN_CONSTANT_P,
        .arguments = &argument,
        .argument_count = 1,
        .node = 148,
        .source_position = UINT32_MAX,
    };
    check_builtin_query_one(table, &complex_query, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "complex value without producer folding proof stays unknown");
    complex_query.complex_constant = true;
    check_builtin_query_one(table, &complex_query, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1) && result.known, "producer proves a folded complex constant without fabricating scalar payload");
    argument.type = BUSTER_TYPE_DOUBLE;
    check_builtin_query_one(table, &complex_query, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "complex witness is restricted to an actual complex operand type");
    argument.type = complex_type;
    argument.flags = BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_query_one(table, &complex_query, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "complex witness does not bypass side-effect exclusion");
    argument.flags = 0;
    argument.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    argument.fault_node = 75;
    check_builtin_query_one(table, &complex_query, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "complex witness does not bypass fault exclusion");
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    const int64_t absolute_offsets[] = { 0, 17, -1 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(absolute_offsets); i += 1)
    {
        argument = check_builtin_query_test_pointer(pointer, 0, absolute_offsets[i]);
        result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
        check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "absolute pointer is placement independent");
    }
    argument = check_builtin_query_test_pointer(BUSTER_TYPE_ULONG, 0, 17);
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "absolute address proof remains valid with integer result type");
    argument = check_builtin_query_test_pointer(BUSTER_TYPE_NULLPTR, 0, 0);
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "nullptr is a known query constant");
    argument = (CheckValue){ .type = BUSTER_TYPE_ULONG, .category = CHECK_RVALUE };
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "unknown integer without absolute-address witness stays unknown");
    const CheckBuiltinQueryAction absolute =
    {
        .id = CBUILTIN_CONSTANT_P,
        .arguments = &argument,
        .argument_count = 1,
        .node = 148,
        .source_position = UINT32_MAX,
        .absolute_address_constant = true,
    };
    check_builtin_query_one(table, &absolute, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "producer absolute-address proof survives a cast that lost constant representation");
    argument.flags = BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_query_one(table, &absolute, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "absolute-address proof does not bypass side-effect exclusion");
    argument.flags = 0;
    argument.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    argument.fault_node = 74;
    check_builtin_query_one(table, &absolute, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "absolute-address proof does not bypass fault exclusion");
    argument = check_builtin_query_test_pointer(pointer, 17, 0);
    result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, &argument, 1);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "ordinary object address is not a constant_p constant");
    CheckBuiltinQueryAction input =
    {
        .id = CBUILTIN_CONSTANT_P,
        .arguments = &argument,
        .argument_count = 1,
        .node = 148,
        .source_position = UINT32_MAX,
        .string_literal_start = true,
    };
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "explicit string-start metadata distinguishes object addresses");
    argument.offset = 1;
    input.string_literal_start = false;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "string interior does not inherit start metadata");
    argument.type = BUSTER_TYPE_ULONG;
    argument.constant_kind = CHECK_CONSTANT_NONE;
    argument.offset = 0;
    input.string_literal_start = true;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "explicit string-start provenance survives an integer cast");
    argument.flags = BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0) && result.known, "string witness does not bypass side-effect exclusion");
    argument.flags = 0;
    argument.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    argument.fault_node = 72;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "string witness does not bypass fault exclusion");
    const TypeId string = type_array(table, BUSTER_TYPE_CHAR, 4);
    argument = check_builtin_query_test_pointer(string, 23, 0);
    argument.category = CHECK_LVALUE;
    argument.flags = BUSTER_CHECK_STATIC_OBJECT;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1) &&
        (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0, "string array decay is retained as conversion metadata");
    argument = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    input.string_literal_start = false;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "unknown scalar yields constant zero");
    argument = check_test_integer(BUSTER_TYPE_INT, 3);
    argument.flags |= BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "constant_p does not evaluate side effects");
    argument = check_test_integer(BUSTER_TYPE_INT, 3);
    argument.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    argument.fault_node = 71;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0), "unevaluated fault does not contaminate query result");
    argument = check_test_integer(type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_VOLATILE), 3);
    argument.category = CHECK_LVALUE;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0) &&
        (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0, "volatile lvalue conversion prevents constant claim without evaluating it");

    CheckValue arguments[2] =
    {
        check_builtin_query_test_pointer(pointer, 31, 0),
        check_test_integer(BUSTER_TYPE_INT, 0),
    };
    input = (CheckBuiltinQueryAction)
    {
        .id = CBUILTIN_OBJECT_SIZE,
        .arguments = arguments,
        .argument_count = 2,
        .node = 148,
        .source_position = UINT32_MAX,
        .object_size_known = { true, true, true, true },
        .object_size_source_constant = { true, true, true, true },
        .object_sizes = { 64, 8, 32, 4 },
    };
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, mode);
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, input.object_sizes[mode]) && result.known && result.mode == mode &&
            result.argument_conversions[1].type == BUSTER_TYPE_INT, "object_size selects independent complete/subobject max/min metadata");
        input.object_size_known[mode] = false;
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, mode < 2 ? UINT64_MAX : 0) && !result.known && result.mode == mode,
            "unknown bound keeps runtime sentinel separate from source constant evaluation");
        input.object_size_known[mode] = true;
    }
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    input.object_sizes[0] = 0;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, 0) && result.known, "known zero is not an unknown bound");
    input.object_sizes[0] = UINT64_MAX;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, UINT64_MAX) && result.known, "known sentinel-sized bound retains its known flag");
    input.object_sizes[0] = 64;
    input.object_size_source_constant[0] = false;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, 64) && result.known,
        "runtime-known whole-object bound need not be source-constant");
    arguments[0].flags = BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, UINT64_MAX) && !result.known,
        "runtime-only bound cannot override effectful pointer source fallback");
    input.object_size_source_constant[0] = true;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, 64) && result.known,
        "source-proven bound survives ignored pointer effects");
    input.object_size_known[0] = false;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, UINT64_MAX) && !result.known,
        "unknown effectful pointer has a source-constant sentinel without executing effects");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 3);
    input.object_size_known[3] = false;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, 0) && !result.known,
        "unknown effectful pointer has minimum-mode source-constant zero");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    arguments[0].flags = 0;
    arguments[0].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    arguments[0].fault_node = 73;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX) && !result.known,
        "faulting unknown pointer is deferred without leaking the fault");
    arguments[0] = (CheckValue){ .type = type_qualify(table, pointer, BUSTER_TYPE_VOLATILE), .category = CHECK_LVALUE };
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_ULONG, UINT64_MAX) && !result.known,
        "unknown volatile pointer read is unevaluated and has a source-constant sentinel");
    arguments[0] = check_builtin_query_test_pointer(pointer, 31, 0);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX) && !result.known,
        "pointee size is never substituted for unknown object extent");
}

static void check_builtin_query_test_constraints(CheckBuiltinQueryTest* test, TypeTable* table)
{
    CheckValue arguments[2] =
    {
        check_builtin_query_test_pointer(type_pointer(table, BUSTER_TYPE_CHAR), 1, 0),
        check_test_integer(BUSTER_TYPE_INT, 0),
    };
    CheckBuiltinQueryResult result = check_builtin_query_test_action(table, CBUILTIN_CONSTANT_P, NULL, 0);
    check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "constant_p requires one argument");
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, NULL, 1);
    check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "object_size requires two arguments");
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, NULL, 2);
    check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "valid arity requires readable argument slice");
    arguments[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1.25L);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 0, "object_size rejects nonpointer floating argument");
    arguments[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX), "ordinary null pointer conversion is accepted");
    arguments[0] = check_builtin_query_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 1, 0);
    const int64_t invalid_modes[] = { -1, 4 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_modes); i += 1)
    {
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, (uint32_t)invalid_modes[i]);
        result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
        check_builtin_query_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 1 && result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_RANGE,
            "object_size mode range is zero through three");
    }
    arguments[1] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE };
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 1 && result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT,
        "object_size mode must be a constant expression");
    arguments[1] = check_test_float(BUSTER_TYPE_DOUBLE, 1.0L);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 1 && result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT,
        "implicit floating conversion does not manufacture an original mode ICE");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX) && result.mode == 1,
        "explicit integer-cast floating literal arrives as an integer ICE");
    arguments[1] = check_test_integer(BUSTER_TYPE_ULLONG, UINT64_C(1) << 32);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX) && result.mode == 0 && result.argument_conversions[1].type == BUSTER_TYPE_INT,
        "mode converts to int before checking zero through three");
    arguments[1] = check_test_integer(BUSTER_TYPE_UINT128, ((unsigned __int128)1 << 96) | 3);
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, 0) && result.mode == 3,
        "wide integer ICE mode retains the int parameter's low bits");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    arguments[1].flags = 0;
    result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
    check_builtin_query_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.constraint == CHECK_BUILTIN_MISC_CONSTRAINT_CONSTANT,
        "folded non-ICE mode retains its original expression constraint");
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148);
    const bool completed = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_query_test_expect(test, completed, "enum query fixture allocation");
    if (completed)
    {
        uint32_t completions[512] = { 0 };
        const uint32_t tag = table->types.index[type_index(enumeration)];
        completions[tag] = 20;
        arguments[0] = check_test_integer(enumeration, 2);
        CheckBuiltinQueryAction input =
        {
            .id = CBUILTIN_CONSTANT_P,
            .arguments = arguments,
            .argument_count = 1,
            .node = 999,
            .tag_completions = completions,
            .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
            .source_position = 19,
        };
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "enum completion remains source-position sensitive in unevaluated query");
        input.source_position = 20;
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 1), "completed enum is a foldable constant");
        arguments[0] = check_builtin_query_test_pointer(type_pointer(table, type_tag(table, BUSTER_TYPE_KIND_STRUCT, 148)), 1, 0);
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, 0);
        result = check_builtin_query_test_action(table, CBUILTIN_OBJECT_SIZE, arguments, 2);
        check_builtin_query_test_expect(test, check_builtin_query_test_deferred(&result, UINT64_MAX), "pointer to incomplete object does not require sizeof its pointee");
    }
}

static void check_builtin_query_test_plans(CheckBuiltinQueryTest* test, TypeTable* table)
{
    CheckValue argument = check_test_float(BUSTER_TYPE_DOUBLE, 0.25L);
    // These conservative source columns may include an unchosen syntactic call/fault. Explicit whole-expression
    // proof and the separate context-pruned runtime summary are authoritative; neither is inferred from this value.
    argument.flags |= BUSTER_CHECK_SIDE_EFFECT;
    argument.fault = CHECK_FAULT_SHIFT_COUNT;
    argument.fault_node = 99;
    CheckBuiltinQueryAction input = { .id = CBUILTIN_CONSTANT_P, .arguments = &argument, .argument_count = 1, .constant_p_context_sensitive = true };
    CheckBuiltinQueryResult result;
    // Original105-row exact23.1.1 baseline-O0 probe, source cae6a6f4 and target log ff539be9. All four ON rounding
    // controls gave the same query classifications; FP work raised MXCSR inexact0x20 and all user counters stayed0.
    const struct
    {
        CheckBuiltinQueryProof source;
        CheckBuiltinQueryRuntimePlan plan;
        uint8_t bit;
        bool fp_effects;
    } observed[] =
    {
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_SKIP, 1, false }, // ON exact1/2.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN, 0, true }, // ON1/3.
        { CHECK_BUILTIN_QUERY_PROOF_ZERO, CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN, 0, true }, // ON dynamic global.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN, 1, true }, // ON nested query.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN, 1, true }, // ON nested query+1.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_SKIP, 0, false }, // ON unchosen call,1/3.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_SKIP, 0, false }, // ON unchosen increment,1/3.
        { CHECK_BUILTIN_QUERY_PROOF_ONE, CHECK_BUILTIN_QUERY_RUNTIME_SKIP, 1, false }, // OFF default1/3.
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(observed); i += 1)
    {
        input.constant_p_source_proof = observed[i].source;
        input.constant_p_runtime_plan = observed[i].plan;
        input.constant_p_runtime_result = observed[i].bit;
        input.constant_p_runtime_effects = (CheckBuiltinQueryEffects){ .reads_fp_environment = observed[i].fp_effects, .may_raise_fp_exceptions = observed[i].fp_effects };
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.source_proof == observed[i].source && result.known &&
            result.action.value.type == BUSTER_TYPE_INT && result.action.value.constant_kind == CHECK_CONSTANT_INTEGER &&
            result.action.value.integer == (observed[i].source == CHECK_BUILTIN_QUERY_PROOF_ONE) && result.action.value.flags == BUSTER_CHECK_ICE &&
            result.action.value.fault == CHECK_FAULT_NONE && result.action.value.fault_node == 0 && result.runtime_plan == observed[i].plan &&
            result.runtime_value.type == BUSTER_TYPE_INT && result.runtime_value.constant_kind == CHECK_CONSTANT_INTEGER && result.runtime_value.integer == observed[i].bit &&
            result.evaluated_operands == (observed[i].fp_effects ? 1u : 0u) && result.runtime_value.flags == (observed[i].fp_effects ? BUSTER_CHECK_SIDE_EFFECT : 0u) &&
            result.runtime_effects.reads_fp_environment == observed[i].fp_effects && result.runtime_effects.may_raise_fp_exceptions == observed[i].fp_effects && result.unsupported == 0,
            "exact target observations retain independent clean source proof, runtime answer and nested FP effects");
    }
    for (uint32_t source = CHECK_BUILTIN_QUERY_PROOF_ZERO; source <= CHECK_BUILTIN_QUERY_PROOF_DEFERRED; source += 1)
    {
        for (uint32_t plan = CHECK_BUILTIN_QUERY_RUNTIME_SKIP; plan <= CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED; plan += 1)
        {
            input.constant_p_source_proof = (CheckBuiltinQueryProof)source;
            input.constant_p_runtime_plan = (CheckBuiltinQueryRuntimePlan)plan;
            const bool runtime_known = plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP || plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
            input.constant_p_runtime_result = runtime_known ? 1 : 0;
            input.constant_p_runtime_effects = (CheckBuiltinQueryEffects){ .flags = BUSTER_CHECK_SIDE_EFFECT, .fault = CHECK_FAULT_DIVIDE_BY_ZERO, .fault_node = 73,
                .reads_fp_environment = true, .may_raise_fp_exceptions = true };
            check_builtin_query_one(table, &input, &result);
            const bool source_known = source != CHECK_BUILTIN_QUERY_PROOF_DEFERRED;
            const bool evaluate = plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN || plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED;
            const bool skip = plan == CHECK_BUILTIN_QUERY_RUNTIME_SKIP;
            const uint32_t unsupported = (source_known ? 0 : BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_SOURCE_PROOF) |
                (plan == CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED ? BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_CONSTANT_TEST :
                 plan == CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED ? BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_RUNTIME_PLAN : 0);
            check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.known == source_known &&
                result.action.value.constant_kind == (source_known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE) &&
                result.action.value.flags == (source_known ? BUSTER_CHECK_ICE : 0u) && result.action.value.fault == CHECK_FAULT_NONE &&
                result.runtime_value.constant_kind == (runtime_known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE) &&
                result.runtime_value.integer == (runtime_known ? 1u : 0u) && result.evaluated_operands == (evaluate ? 1u : 0u) &&
                result.runtime_value.flags == (skip ? 0u : BUSTER_CHECK_SIDE_EFFECT) && result.runtime_value.fault == (skip ? CHECK_FAULT_NONE : CHECK_FAULT_DIVIDE_BY_ZERO) &&
                result.runtime_value.fault_node == (skip ? 0u : 73u) && result.runtime_effects.reads_fp_environment == !skip &&
                result.runtime_effects.may_raise_fp_exceptions == !skip && result.unsupported == unsupported,
                "independent proof/plan matrix suppresses SKIP effects and retains known/deferred/unresolved runtime requirements");
        }
    }
    input.constant_p_source_proof = CHECK_BUILTIN_QUERY_PROOF_ONE;
    input.constant_p_runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN;
    input.constant_p_runtime_result = 1;
    input.constant_p_runtime_effects = (CheckBuiltinQueryEffects){ .reads_fp_environment = true, .may_raise_fp_exceptions = true };
    CheckBuiltinQueryResult inner;
    check_builtin_query_one(table, &input, &inner);
    argument = inner.action.value;
    input.constant_p_runtime_effects = inner.runtime_effects;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, result.runtime_value.integer == 1 && result.evaluated_operands == 1 &&
        result.runtime_effects.reads_fp_environment && result.runtime_effects.may_raise_fp_exceptions,
        "nested source-clean queries retain inner runtime FP work through a known outer answer");
    input.constant_p_runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_SKIP;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, result.runtime_effects.flags == 0 && result.runtime_effects.fault == CHECK_FAULT_NONE && result.runtime_effects.fault_node == 0 &&
        !result.runtime_effects.reads_fp_environment && !result.runtime_effects.may_raise_fp_exceptions && result.evaluated_operands == 0 && result.runtime_value.flags == 0,
        "an outer SKIP plan prunes nested runtime effects instead of evaluating its operand");
    for (uint32_t supplied = 0; supplied < 4; supplied += 1)
    {
        input = (CheckBuiltinQueryAction){ .id = CBUILTIN_CONSTANT_P, .arguments = &argument, .argument_count = 1, .constant_p_context_sensitive = true,
            .constant_p_source_proof = (supplied & 1) != 0 ? CHECK_BUILTIN_QUERY_PROOF_ONE : CHECK_BUILTIN_QUERY_PROOF_INFER,
            .constant_p_runtime_plan = (supplied & 2) != 0 ? CHECK_BUILTIN_QUERY_RUNTIME_SKIP : CHECK_BUILTIN_QUERY_RUNTIME_INFER };
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
            result.runtime_plan == (supplied == 3 ? CHECK_BUILTIN_QUERY_RUNTIME_SKIP : CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED) &&
            result.known == ((supplied & 1) != 0) &&
            ((result.unsupported & BUSTER_CHECK_BUILTIN_QUERY_UNSUPPORTED_RUNTIME_PLAN) != 0) == (supplied != 3),
            "context-sensitive queries require independent explicit source proof and runtime plan");
    }
    for (uint32_t invalid = 0; invalid < 9; invalid += 1)
    {
        input = (CheckBuiltinQueryAction){ .id = CBUILTIN_CONSTANT_P, .arguments = &argument, .argument_count = 1,
            .constant_p_source_proof = CHECK_BUILTIN_QUERY_PROOF_ONE, .constant_p_runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_SKIP };
        if (invalid == 0)
        {
            input.constant_p_source_proof = (CheckBuiltinQueryProof)UINT32_MAX;
        }
        else if (invalid == 1)
        {
            input.constant_p_runtime_plan = (CheckBuiltinQueryRuntimePlan)UINT32_MAX;
        }
        else if (invalid == 2)
        {
            input.constant_p_runtime_result = 2;
        }
        else if (invalid < 6)
        {
            input.constant_p_runtime_plan = invalid == 3 ? CHECK_BUILTIN_QUERY_RUNTIME_INFER :
                invalid == 4 ? CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED : CHECK_BUILTIN_QUERY_RUNTIME_UNRESOLVED;
            input.constant_p_runtime_result = 1;
        }
        else if (invalid == 6)
        {
            input.constant_p_runtime_effects.flags = BUSTER_CHECK_ICE;
        }
        else if (invalid == 7)
        {
            input.constant_p_runtime_effects.fault = (CheckFault)UINT32_MAX;
        }
        else
        {
            input.constant_p_runtime_effects.fault_node = 9;
        }
        check_builtin_query_one(table, &input, &result);
        check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.runtime_value.constant_kind == CHECK_CONSTANT_NONE,
            "invalid proof/plan enums, known bits and effect-summary shapes fail closed");
    }
    argument = (CheckValue){ .type = type_vector(table, BUSTER_TYPE_INT, 4, false), .category = CHECK_RVALUE };
    input = (CheckBuiltinQueryAction){ .id = CBUILTIN_CONSTANT_P, .arguments = &argument, .argument_count = 1,
        .constant_p_source_proof = CHECK_BUILTIN_QUERY_PROOF_ONE, .constant_p_runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_SKIP };
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE,
        "an explicit ONE certificate cannot invent Clang source folding for an excluded vector type");
    input.constant_p_source_proof = CHECK_BUILTIN_QUERY_PROOF_ZERO;
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_constant(&result, BUSTER_TYPE_INT, 0),
        "an excluded constant_p folding type remains valid source with a ZERO proof");
    CheckValue object_arguments[2] = { check_builtin_query_test_pointer(type_pointer(table, BUSTER_TYPE_CHAR), 3, 0), check_test_integer(BUSTER_TYPE_INT, 1) };
    input = (CheckBuiltinQueryAction){ .id = CBUILTIN_OBJECT_SIZE, .arguments = object_arguments, .argument_count = 2 };
    CheckBuiltinQueryResult object_before;
    check_builtin_query_one(table, &input, &object_before);
    input.constant_p_context_sensitive = true;
    input.constant_p_source_proof = (CheckBuiltinQueryProof)UINT32_MAX;
    input.constant_p_runtime_plan = (CheckBuiltinQueryRuntimePlan)UINT32_MAX;
    input.constant_p_runtime_result = UINT8_MAX;
    input.constant_p_runtime_effects = (CheckBuiltinQueryEffects){ .flags = UINT32_MAX, .fault_node = 99 };
    check_builtin_query_one(table, &input, &result);
    check_builtin_query_test_expect(test, check_builtin_query_test_equal(&object_before, &result) && result.source_proof == CHECK_BUILTIN_QUERY_PROOF_INFER &&
        result.runtime_plan == CHECK_BUILTIN_QUERY_RUNTIME_INFER && result.unsupported == 0,
        "constant_p-only metadata cannot change any object_size result or bound/sentinel semantics");
}

static void check_builtin_query_test_pages(CheckBuiltinQueryTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH * sizeof(CheckBuiltinQueryAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH * sizeof(CheckBuiltinQueryResult) + 16 + page - 1) / page) * page : 0;
    const size_t argument_region = page != 0 ? ((2 * BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t bytes = input_region + output_region + argument_region + 3 * page;
    uint8_t* mapping = page != 0 ? mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    uint8_t* argument_end = ready ? output_end + page + argument_region : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0 && mprotect(argument_end, page, PROT_NONE) == 0;
    }
    check_builtin_query_test_expect(test, ready, "guarded query action/result/argument slices allocated");
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_CHAR);
    check_builtin_query(NULL, NULL, NULL, 0);
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH; count += 1)
    {
        const uint32_t argument_count = count + count / 2;
        CheckBuiltinQueryAction* actions = (CheckBuiltinQueryAction*)(input_end - count * sizeof(*actions));
        CheckBuiltinQueryResult* results = (CheckBuiltinQueryResult*)(output_end - count * sizeof(*results));
        CheckValue* arguments = (CheckValue*)(argument_end - argument_count * sizeof(*arguments));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        memset((uint8_t*)arguments - 16, 0xa5, 16 + argument_count * sizeof(*arguments));
        CheckBuiltinQueryResult expected[BUSTER_CHECK_BUILTIN_QUERY_TEST_BATCH];
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const bool object = i % 2 != 0;
            arguments[cursor] = object ? check_builtin_query_test_pointer(pointer, i + 1, 0) : check_test_integer(BUSTER_TYPE_INT, i);
            if (object)
            {
                arguments[cursor + 1] = check_test_integer(BUSTER_TYPE_INT, i % 4);
            }
            actions[i] = (CheckBuiltinQueryAction)
            {
                .id = object ? CBUILTIN_OBJECT_SIZE : CBUILTIN_CONSTANT_P,
                .arguments = arguments + cursor,
                .argument_count = object ? 2 : 1,
                .node = i,
                .source_position = UINT32_MAX,
                .object_size_known = { true, true, true, true },
                .object_size_source_constant = { true, true, true, true },
                .object_sizes = { 64, 8, 32, 4 },
            };
            if (!object && i % 4 == 2)
            {
                const uint32_t variant = (i / 4) % 4;
                actions[i].constant_p_context_sensitive = true;
                actions[i].constant_p_source_proof = variant == 1 ? CHECK_BUILTIN_QUERY_PROOF_DEFERRED : CHECK_BUILTIN_QUERY_PROOF_ONE;
                actions[i].constant_p_runtime_plan = variant == 0 ? CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN :
                    variant == 1 ? CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_DEFERRED : variant == 2 ? CHECK_BUILTIN_QUERY_RUNTIME_INFER : CHECK_BUILTIN_QUERY_RUNTIME_SKIP;
                actions[i].constant_p_runtime_effects = (CheckBuiltinQueryEffects){ .reads_fp_environment = true, .may_raise_fp_exceptions = true };
            }
            check_builtin_query_one(table, actions + i, expected + i);
            cursor += object ? 2 : 1;
        }
        check_builtin_query(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_query_test_expect(test, check_builtin_query_test_equal(expected + i, results + i), "guarded batch equals individual queries");
        }
        for (uint32_t partition = 1; partition <= 5; partition += 1)
        {
            for (uint32_t first = 0; first < count; first += partition)
            {
                const uint32_t remaining = count - first < partition ? count - first : partition;
                check_builtin_query(table, actions + first, results + first, remaining);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_query_test_expect(test, check_builtin_query_test_equal(expected + i, results + i), "legal query batch partition preserves all metadata");
            }
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_query_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5 && *((uint8_t*)arguments - i) == 0xa5,
                "query batch preserves bytes before logical slices");
        }
    }
    if (ready)
    {
        CheckValue* last_argument = (CheckValue*)argument_end - 1;
        *last_argument = check_test_integer(BUSTER_TYPE_INT, 1);
        CheckBuiltinQueryAction tail = { .id = CBUILTIN_CONSTANT_P, .arguments = last_argument, .argument_count = 1, .constant_p_context_sensitive = true,
            .constant_p_source_proof = CHECK_BUILTIN_QUERY_PROOF_ONE, .constant_p_runtime_plan = CHECK_BUILTIN_QUERY_RUNTIME_EVALUATE_KNOWN,
            .constant_p_runtime_result = 1, .constant_p_runtime_effects = { .reads_fp_environment = true, .may_raise_fp_exceptions = true } };
        CheckBuiltinQueryResult tail_result;
        check_builtin_query_one(table, &tail, &tail_result);
        check_builtin_query_test_expect(test, tail_result.action.diagnostic == CHECK_DIAG_NONE && tail_result.evaluated_operands == 1 && tail_result.runtime_value.integer == 1 &&
            tail_result.runtime_value.flags == BUSTER_CHECK_SIDE_EFFECT, "one explicit runtime operand ends exactly at a protected page");
        tail.arguments = (const CheckValue*)argument_end;
        tail.constant_p_source_proof = (CheckBuiltinQueryProof)UINT32_MAX;
        check_builtin_query_one(table, &tail, &tail_result);
        check_builtin_query_test_expect(test, tail_result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "malformed query proof is rejected before an inaccessible operand read");
        const CBuiltinId ids[] = { CBUILTIN_CONSTANT_P, CBUILTIN_OBJECT_SIZE };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
        {
            const CheckBuiltinQueryAction input =
            {
                .id = ids[i],
                .arguments = (const CheckValue*)argument_end,
                .argument_count = 3,
            };
            CheckBuiltinQueryResult result;
            check_builtin_query_one(table, &input, &result);
            check_builtin_query_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "arity rejected before inaccessible argument read");
        }
    }
    check_builtin_query(NULL, NULL, NULL, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

static void check_builtin_query_test_clang(CheckBuiltinQueryTest* test, const char* clang)
{
    static const struct
    {
        const char* source;
        bool rejected;
    } cases[] =
    {
        { "_Static_assert(_Generic(__builtin_constant_p(7), int:1, default:0), \"int\");\n_Static_assert(__builtin_constant_p(3+4)==1, \"integer\");\n_Static_assert(__builtin_constant_p(1.25)==1, \"floating\");\n_Static_assert(__builtin_constant_p((1,3))==1, \"folded non ICE\");\n", false },
        { "int x; _Static_assert(__builtin_constant_p(&x)==0, \"object\");\n_Static_assert(__builtin_constant_p((void*)0)==1, \"null\");\n_Static_assert(__builtin_constant_p((void*)17)==1, \"absolute\");\n_Static_assert(__builtin_constant_p((unsigned long)(void*)17)==1, \"absolute cast\");\n", false },
        { "_Static_assert(__builtin_constant_p(\"abc\")==1, \"string\");\n_Static_assert(__builtin_constant_p(\"abc\"+1)==0, \"interior\");\n_Static_assert(__builtin_constant_p((unsigned long)\"abc\")==1, \"string cast\");\n", false },
        { "int x; _Static_assert(__builtin_constant_p(x++)==0, \"effect\");\n_Static_assert(__builtin_constant_p((x++,3))==0, \"comma effect\");\n_Static_assert(__builtin_constant_p(1/0)==0, \"fault\");\n", false },
        { "volatile int x; _Static_assert(__builtin_constant_p(x)==0, \"volatile\");\n", false },
        { "_Static_assert(__builtin_constant_p(1.0+2.0i)==1, \"complex literal\");\n", false },
        { "extern double _Complex z; _Static_assert(__builtin_constant_p(z)==0, \"nonconstant complex\");\n", false },
        { "int x; _Static_assert(__builtin_constant_p((x++,1.0+2.0i))==0, \"effectful complex\");\n", false },
        { "int f(void) { return __builtin_constant_p(); }\n", true },
        { "int f(void) { return __builtin_constant_p(1,2); }\n", true },
        { "_Static_assert(_Generic(__builtin_object_size(\"abc\",0), unsigned long:1, default:0), \"size_t\");\n_Static_assert(__builtin_object_size(\"abc\",1)==4, \"string extent\");\n", false },
        { "struct S { char a[4]; char b[8]; }; struct S s;\n_Static_assert(__builtin_object_size(s.a+1,0)==11, \"whole max\");\n_Static_assert(__builtin_object_size(s.a+1,1)==3, \"member max\");\n_Static_assert(__builtin_object_size(s.a+1,2)==11, \"whole min\");\n_Static_assert(__builtin_object_size(s.a+1,3)==3, \"member min\");\n", false },
        { "extern char* p;\n_Static_assert(__builtin_object_size(p,0)==(__SIZE_TYPE__)-1, \"unknown max\");\n_Static_assert(__builtin_object_size(p,1)==(__SIZE_TYPE__)-1, \"unknown member max\");\n_Static_assert(__builtin_object_size(p,2)==0, \"unknown min\");\n_Static_assert(__builtin_object_size(p,3)==0, \"unknown member min\");\n", true },
        { "char* p; _Static_assert(__builtin_object_size(p++,0)==(__SIZE_TYPE__)-1, \"effect unknown\");\n", false },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,0x100000000ULL); }\n", false },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,(int)1.0); }\n", false },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,1.0); }\n", true },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,-1); }\n", true },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,4); }\n", true },
        { "__SIZE_TYPE__ f(char* p,int mode) { return __builtin_object_size(p,mode); }\n", true },
        { "__SIZE_TYPE__ f(void) { return __builtin_object_size(1.0,0); }\n", true },
        { "_Static_assert(__builtin_object_size(\"abc\",0)==4, \"runtime only\");\n", true },
        { "_Static_assert(__builtin_constant_p(__builtin_object_size(\"abc\",0))==0, \"deferred literal\");\n", false },
        { "extern char* p; __SIZE_TYPE__ n = __builtin_object_size(p,0);\n", true },
        { "extern char* p; _Static_assert(1 ? 1 : __builtin_object_size(p,0), \"unevaluated\");\n", false },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,0); }\n", false },
        { "char a[4]; int i; _Static_assert(__builtin_object_size((i++,a),0)==4, \"known despite effects\");\n", false },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p); }\n", true },
        { "__SIZE_TYPE__ f(char* p) { return __builtin_object_size(p,0,1); }\n", true },
        { "_Static_assert(__clang_major__>23 || (__clang_major__==23 && (__clang_minor__>1 || (__clang_minor__==1 && __clang_patchlevel__>=1))),\"Clang 23.1.1 or newer required\");\n"
          "#pragma STDC FENV_ACCESS ON\n"
          "extern double call(void); extern double dynamic; extern int n;\n"
          "_Static_assert(__builtin_constant_p(1.0/2.0)==1,\"source exact\");\n"
          "_Static_assert(__builtin_constant_p(1.0/3.0)==1,\"source inexact\");\n"
          "_Static_assert(__builtin_constant_p(1?1.0/3.0:call())==1,\"fold before syntactic call\");\n"
          "_Static_assert(__builtin_constant_p(1?7:n++)==1,\"fold before unchosen increment\");\n"
          "_Static_assert(__builtin_constant_p(__builtin_constant_p(1.0/3.0))==1,\"nested source proof\");\n"
          "_Static_assert(__builtin_constant_p((n++,1.0/3.0))==0,\"chosen increment\");\n"
          "_Static_assert(__builtin_constant_p(call())==0,\"source call\");\n"
          "_Static_assert(__builtin_constant_p(dynamic+1.0/3.0)==0,\"source dynamic\");\n", false },
    };
    char directory[] = "/tmp/buster-check-builtin-query-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
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
    check_builtin_query_test_expect(test, ready, "independent Clang query source assertions and constraints ran");
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        if (results[i].rejected != cases[i].rejected)
        {
            fprintf(stderr, "test-check-builtins-query: fixture %u expected %s, got %s at %u:%u: %s\n", i,
                    cases[i].rejected ? "rejection" : "acceptance", results[i].rejected ? "rejection" : "acceptance",
                    results[i].line, results[i].column, results[i].message);
        }
        check_builtin_query_test_expect(test, results[i].rejected == cases[i].rejected, "Clang agrees with original query source fixture");
    }
    for (uint32_t i = 0; created && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (created)
    {
        rmdir(directory);
    }
}

static int run_check_builtin_query_tests(int argc, char** argv)
{
    CheckBuiltinQueryTest test = { 0 };
    const char* clang = "clang";
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            i += 1;
            clang = argv[i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    check_builtin_query_test_expect(&test, options, "usage: test-check-builtins-query [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_query_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_builtin_query_test_values(&test, &types.table);
            check_builtin_query_test_constraints(&test, &types.table);
            check_builtin_query_test_plans(&test, &types.table);
            check_builtin_query_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_query_test_clang(&test, clang);
    }
    printf("test-check-builtins-query: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
