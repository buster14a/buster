// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original floating-builtin action fixtures (#148). Include after c_checker_test.c and c_check_builtin_float.c.
// Native bootstrap Clang builtins supply an independent payload/classification oracle; separate C sources check
// constraints and constant expressions. Existing arena/type and fieldwise comparison helpers are reused. No
// external implementation, payload table or fixture corpus is imported. Tests do not claim Buster source support.

#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef struct CheckBuiltinFloatTest CheckBuiltinFloatTest;
struct CheckBuiltinFloatTest
{
    uint64_t checks;
    uint64_t failures;
};

static void check_builtin_float_test_expect(CheckBuiltinFloatTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtin-float: %s\n", description);
        }
    }
}

static bool check_builtin_float_test_equal(const CheckBuiltinFloatResult* a, const CheckBuiltinFloatResult* b)
{
    bool equal = check_test_action_equal(&a->action, &b->action) && a->floating_type == b->floating_type &&
                 a->evaluated_operands == b->evaluated_operands && a->definite_operands == b->definite_operands &&
                 a->selected_selector == b->selected_selector && a->unsupported == b->unsupported && a->warnings == b->warnings &&
                 a->runtime_builtin == b->runtime_builtin && a->evaluation == b->evaluation && a->folded == b->folded && a->reads_memory == b->reads_memory;
    for (uint32_t i = 0; equal && i < 6; i += 1)
    {
        equal = a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    for (uint32_t i = 0; equal && i < 5; i += 1)
    {
        equal = check_test_value_equal(a->selectors + i, b->selectors + i);
    }
    return equal;
}

static bool check_builtin_float_test_bits(TypeId type, long double a, long double b)
{
    bool equal;
    if (type == BUSTER_TYPE_FLOAT)
    {
        const float aa = a;
        const float bb = b;
        uint32_t ab;
        uint32_t ba;
        memcpy(&ab, &aa, sizeof(ab));
        memcpy(&ba, &bb, sizeof(ba));
        equal = ab == ba;
    }
    else if (type == BUSTER_TYPE_DOUBLE)
    {
        const double aa = a;
        const double bb = b;
        uint64_t ab;
        uint64_t ba;
        memcpy(&ab, &aa, sizeof(ab));
        memcpy(&ba, &bb, sizeof(ba));
        equal = ab == ba;
    }
    else
    {
        uint8_t aa[16];
        uint8_t bb[16];
        memcpy(aa, &a, sizeof(aa));
        memcpy(bb, &b, sizeof(bb));
        equal = memcmp(aa, bb, 10) == 0;
    }
    return equal;
}

static void check_builtin_float_test_constants(CheckBuiltinFloatTest* test, TypeTable* table)
{
    const CBuiltinId specials[] = {
        CBUILTIN_HUGE_VAL,
        CBUILTIN_HUGE_VALF,
        CBUILTIN_HUGE_VALL,
        CBUILTIN_INF,
        CBUILTIN_INFF,
        CBUILTIN_INFL,
    };
    const TypeId types[] = {
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_LONG_DOUBLE,
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_LONG_DOUBLE,
    };
    const long double oracle[] = {
        __builtin_huge_val(),
        __builtin_huge_valf(),
        __builtin_huge_vall(),
        __builtin_inf(),
        __builtin_inff(),
        __builtin_infl(),
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(specials); i += 1)
    {
        const CheckBuiltinFloatAction action = { .id = specials[i] };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == types[i] &&
                                        result.action.value.constant_kind == CHECK_CONSTANT_FLOATING && result.folded && result.evaluated_operands == 0 &&
                                        check_builtin_float_test_bits(types[i], result.action.value.floating, oracle[i]), "infinity builtin matches Clang's exact source format");
    }
    const char* payloads[] = {
        "",
        "1",
        "0x12345",
        "0b101001",
        "0o765",
        "0777",
        "340282366920938463463374607431768211456123456789",
        "0xffffffffffffffffffffffffffffffffffffffffffffffff",
    };
    const long double float_oracle[] = {
        __builtin_nanf(""), __builtin_nanf("1"), __builtin_nanf("0x12345"), __builtin_nanf("0b101001"),
        __builtin_nanf("0o765"), __builtin_nanf("0777"), __builtin_nanf("340282366920938463463374607431768211456123456789"),
        __builtin_nanf("0xffffffffffffffffffffffffffffffffffffffffffffffff"),
    };
    const long double double_oracle[] = {
        __builtin_nan(""), __builtin_nan("1"), __builtin_nan("0x12345"), __builtin_nan("0b101001"),
        __builtin_nan("0o765"), __builtin_nan("0777"), __builtin_nan("340282366920938463463374607431768211456123456789"),
        __builtin_nan("0xffffffffffffffffffffffffffffffffffffffffffffffff"),
    };
    const long double long_oracle[] = {
        __builtin_nanl(""), __builtin_nanl("1"), __builtin_nanl("0x12345"), __builtin_nanl("0b101001"),
        __builtin_nanl("0o765"), __builtin_nanl("0777"), __builtin_nanl("340282366920938463463374607431768211456123456789"),
        __builtin_nanl("0xffffffffffffffffffffffffffffffffffffffffffffffff"),
    };
    CheckValue string = {
        .type = type_array(table, BUSTER_TYPE_CHAR, 80),
        .category = CHECK_LVALUE,
        .flags = BUSTER_CHECK_STATIC_OBJECT,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = 91,
    };
    for (uint32_t format = 0; format < 3; format += 1)
    {
        const TypeId type = format == 0 ? BUSTER_TYPE_FLOAT : format == 1 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_LONG_DOUBLE;
        const CBuiltinId id = format == 0 ? CBUILTIN_NANF : format == 1 ? CBUILTIN_NAN : CBUILTIN_NANL;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(payloads); i += 1)
        {
            const CheckBuiltinFloatAction action = {
                .id = id,
                .arguments = &string,
                .argument_count = 1,
                .nan_payload = (const uint8_t*)payloads[i],
                .nan_payload_length = strlen(payloads[i]),
                .nan_payload_known = true,
            };
            CheckBuiltinFloatResult result;
            check_builtin_float_one(table, &action, &result);
            const long double expected = format == 0 ? float_oracle[i] : format == 1 ? double_oracle[i] : long_oracle[i];
            check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == type && result.folded &&
                                            !result.reads_memory && result.runtime_builtin == CBUILTIN_NONE &&
                                            check_builtin_float_test_bits(type, result.action.value.floating, expected), "bounded arbitrary-width NaN payload matches Clang bits");
        }
    }
    const char* invalid[] = { " 1", "+1", "-1", "0x", "0b", "0O7", "08", "1x", "nan", "1'2" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        const CheckBuiltinFloatAction action = {
            .id = CBUILTIN_NAN,
            .arguments = &string,
            .argument_count = 1,
            .nan_payload = (const uint8_t*)invalid[i],
            .nan_payload_length = strlen(invalid[i]),
            .nan_payload_known = true,
        };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.reads_memory &&
                                        result.runtime_builtin == CBUILTIN_NAN && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
                                        (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_INVALID_PAYLOAD) != 0,
                                        "invalid numeric payload remains a typed runtime call instead of a fabricated NaN");
    }
    const uint8_t embedded[] = { '1', 0, '2' };
    CheckBuiltinFloatAction action = {
        .id = CBUILTIN_NAN,
        .arguments = &string,
        .argument_count = 1,
        .nan_payload = embedded,
        .nan_payload_length = sizeof(embedded),
        .nan_payload_known = true,
    };
    CheckBuiltinFloatResult result;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, !result.folded && (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_INVALID_PAYLOAD) != 0,
                                    "embedded decoded NUL does not truncate the payload metadata slice");
    action.nan_payload_known = false;
    string.flags |= BUSTER_CHECK_SIDE_EFFECT;
    string.fault = CHECK_FAULT_SHIFT_COUNT;
    string.fault_node = 19;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.fault == CHECK_FAULT_SHIFT_COUNT &&
                                    result.action.value.fault_node == 19 && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                    result.evaluated_operands == 1 && (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNKNOWN_PAYLOAD) != 0,
                                    "runtime NaN call preserves the actual pointer expression's effects and fault");
}

static void check_builtin_float_test_classification(CheckBuiltinFloatTest* test, TypeTable* table)
{
    const TypeId formats[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    const long double samples[] = {
        0.0L,
        -0.0L,
        1.0L,
        -1.0L,
        0x1p-149L,
        0x1p-1074L,
        0x1p-16445L,
        __builtin_infl(),
        -__builtin_infl(),
        __builtin_nanl("1"),
        -__builtin_nanl("1"),
    };
    for (uint32_t format = 0; format < BUSTER_ARRAY_LENGTH(formats); format += 1)
    {
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(samples); s += 1)
        {
            const long double number = check_action_round_float(formats[format], samples[s]);
            CheckValue value = check_test_float(formats[format], number);
            const CBuiltinId unary[] = { CBUILTIN_ISNAN, CBUILTIN_ISINF, CBUILTIN_ISFINITE, CBUILTIN_SIGNBIT, CBUILTIN_ISINF_SIGN };
            for (uint32_t op = 0; op < BUSTER_ARRAY_LENGTH(unary); op += 1)
            {
                const CheckBuiltinFloatAction action = {
                    .id = unary[op],
                    .arguments = &value,
                    .argument_count = 1,
                };
                CheckBuiltinFloatResult result;
                check_builtin_float_one(table, &action, &result);
                const uint32_t expected = op == 0 ? !!__builtin_isnan(number) : op == 1 ? !!__builtin_isinf(number) :
                                          op == 2 ? !!__builtin_isfinite(number) : op == 3 ? !!__builtin_signbit(number) : (uint32_t)__builtin_isinf_sign(number);
                check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_INT &&
                                                result.floating_type == formats[format] && result.folded && result.action.value.integer == expected &&
                                                (result.action.value.flags & BUSTER_CHECK_ICE) != 0, "classification preserves source type and matches Clang's oracle");
            }
            const CheckValue arguments[] = {
                check_test_integer(BUSTER_TYPE_INT, 11), check_test_integer(BUSTER_TYPE_INT, 22), check_test_integer(BUSTER_TYPE_INT, 33),
                check_test_integer(BUSTER_TYPE_INT, 44), check_test_integer(BUSTER_TYPE_INT, 55), value,
            };
            const CheckBuiltinFloatAction action = {
                .id = CBUILTIN_FPCLASSIFY,
                .arguments = arguments,
                .argument_count = BUSTER_ARRAY_LENGTH(arguments),
            };
            CheckBuiltinFloatResult result;
            check_builtin_float_one(table, &action, &result);
            const int expected = format == 0 ? __builtin_fpclassify(11, 22, 33, 44, 55, (float)number) :
                                 format == 1 ? __builtin_fpclassify(11, 22, 33, 44, 55, (double)number) : __builtin_fpclassify(11, 22, 33, 44, 55, number);
            check_builtin_float_test_expect(test, result.folded && result.action.value.integer == (uint32_t)expected && result.floating_type == formats[format],
                                            "fpclassify normal/subnormal boundary matches Clang for each actual source format");
        }
    }
    CheckValue arguments[6] = {
        check_test_integer(BUSTER_TYPE_INT, 11),
        check_test_integer(BUSTER_TYPE_INT, 22),
        check_test_integer(BUSTER_TYPE_INT, 33),
        check_test_integer(BUSTER_TYPE_INT, 44),
        check_test_integer(BUSTER_TYPE_INT, 55),
        check_test_float(BUSTER_TYPE_DOUBLE, 0),
    };
    CheckBuiltinFloatAction action = {
        .id = CBUILTIN_FPCLASSIFY,
        .arguments = arguments,
        .argument_count = 6,
    };
    CheckBuiltinFloatResult result;
    const long double values[] = { __builtin_nanl(""), __builtin_infl(), 1.0L, 0x1p-1074L, -0.0L };
    for (uint32_t category = 0; category < 5; category += 1)
    {
        arguments[5] = check_test_float(BUSTER_TYPE_DOUBLE, values[category]);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.selected_selector == category && result.folded &&
                                        result.action.value.integer == 11 * (category + 1) &&
                                        result.evaluated_operands == (32u | (1u << category)) &&
                                        result.evaluation == CHECK_BUILTIN_FLOAT_EVALUATION_FOLDED_SELECTOR,
                                        "fpclassify folds the selected int selector using the actual source format");
    }
    arguments[0].flags |= BUSTER_CHECK_SIDE_EFFECT;
    arguments[0].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    arguments[0].fault_node = 99;
    arguments[5] = check_test_float(BUSTER_TYPE_DOUBLE, 0);
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.folded && result.action.value.integer == 55 && result.action.value.fault == CHECK_FAULT_NONE &&
                                    (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0,
                                    "Clang's successful early fold ignores every unselected selector's effects and fault");
    arguments[2].flags |= BUSTER_CHECK_SIDE_EFFECT;
    arguments[5] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, !result.folded && result.evaluation == CHECK_BUILTIN_FLOAT_EVALUATION_FPCLASSIFY_RUNTIME &&
                                    result.evaluated_operands == 63 && result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO &&
                                    (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
                                    "failed early fold retains the runtime normal-class prefix and both final selector evaluations");
    arguments[5].constant_kind = CHECK_CONSTANT_NONE;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, !result.folded && result.evaluated_operands == 63 && result.definite_operands == 48 &&
                                    result.action.value.fault == CHECK_FAULT_NONE && result.selectors[0].fault == CHECK_FAULT_DIVIDE_BY_ZERO,
                                    "unknown runtime class retains conditional selector faults without making them unconditional");
    arguments[4].fault = CHECK_FAULT_SHIFT_COUNT;
    arguments[4].fault_node = 73;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.value.fault == CHECK_FAULT_SHIFT_COUNT && result.action.value.fault_node == 73,
                                    "ZERO selector is definitely evaluated on every nonfolded runtime path");
    arguments[5].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    arguments[5].fault_node = 42;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.action.value.fault_node == 42,
                                    "floating argument's earlier fault precedes ZERO selector's evaluation fault");
    arguments[0] = check_test_float(BUSTER_TYPE_DOUBLE, 12.75L);
    arguments[2].flags = BUSTER_CHECK_ICE;
    arguments[4].fault = CHECK_FAULT_NONE;
    arguments[5] = check_test_float(BUSTER_TYPE_DOUBLE, __builtin_nan(""));
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.folded && result.action.value.integer == 12 && result.selectors[0].type == BUSTER_TYPE_INT &&
                                    result.argument_conversions[0].type == BUSTER_TYPE_INT && result.argument_conversions[5].type == BUSTER_TYPE_DOUBLE,
                                    "selector prototypes convert arithmetic arguments to int while leaving FP argument unpromoted");
}

static void check_builtin_float_test_absolute(CheckBuiltinFloatTest* test, TypeTable* table)
{
    const CBuiltinId ids[] = { CBUILTIN_FABSF, CBUILTIN_FABS, CBUILTIN_FABSL };
    const TypeId formats[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    const long double samples[] = { -0.0L, 0.0L, -1.75L, -0x1.000001p0L, -0x1p-149L, -0x1p-1074L, -0x1p-16445L, -__builtin_infl(), -__builtin_nanl("123") };
    for (uint32_t target = 0; target < BUSTER_ARRAY_LENGTH(ids); target += 1)
    {
        for (uint32_t source = 0; source < BUSTER_ARRAY_LENGTH(formats); source += 1)
        {
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(samples); i += 1)
            {
                const long double number = check_action_round_float(formats[source], samples[i]);
                CheckValue value = check_test_float(formats[source], number);
                const CheckBuiltinFloatAction action = { .id = ids[target], .arguments = &value, .argument_count = 1 };
                CheckBuiltinFloatResult result;
                check_builtin_float_one(table, &action, &result);
                const long double expected = target == 0 ? __builtin_fabsf((float)number) : target == 1 ? __builtin_fabs((double)number) : __builtin_fabsl(number);
                check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.folded && result.action.value.type == formats[target] &&
                    result.floating_type == formats[target] && result.argument_conversions[0].type == formats[target] &&
                    (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && result.evaluated_operands == 1 && result.definite_operands == 1 &&
                    result.action.value.constant_kind == CHECK_CONSTANT_FLOATING && check_builtin_float_test_bits(formats[target], result.action.value.floating, expected) &&
                    (result.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL)) == 0,
                    "fabs converts to its declared format before exact sign removal and never invents integer/literal provenance");
                const bool warning = type_size(table, formats[source]) > type_size(table, formats[target]);
                check_builtin_float_test_expect(test, ((result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0) == warning,
                    "fabs narrowing warning follows source storage size");
            }
        }
        CheckValue integer = check_test_integer(BUSTER_TYPE_INT, (uint32_t)-7);
        CheckBuiltinFloatAction action = { .id = ids[target], .arguments = &integer, .argument_count = 1 };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && result.action.value.floating == 7 && result.action.value.type == formats[target] &&
            (result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0, "fabs accepts a signed integer with Clang's absolute-value warning");
        integer.type = BUSTER_TYPE_UINT128;
        integer.integer = (unsigned __int128)1 << 100;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && result.action.value.floating == 0x1p100L &&
            (result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0, "unsigned 128-bit fabs argument warns but converts normally");
        integer.type = BUSTER_TYPE_INT128;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && result.warnings == 0, "signed 128-bit fabs has no available integer-abs replacement warning");
        integer = check_test_integer(BUSTER_TYPE_BOOL, 1);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && result.action.value.floating == 1 &&
            (result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0, "fabs accepts bool through the declared FP parameter conversion");
        const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 14895 + target);
        check_builtin_float_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0), "fabs enum input fixture");
        integer = check_test_integer(enumeration, (uint32_t)-7);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && result.action.value.floating == 7, "fabs accepts an enum using its actual signed underlying type");
        const TypeId unsigned_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 14898 + target);
        check_builtin_float_test_expect(test, type_enum_complete(table, unsigned_enum, BUSTER_TYPE_UINT128, 0), "fabs unsigned enum input fixture");
        integer = check_test_integer(unsigned_enum, 7);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.folded && (result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0,
            "fabs unsigned enum warns even when no equally wide signed absolute-value replacement exists");
        integer = (CheckValue){ .type = type_bit_int(table, 256, false), .category = CHECK_RVALUE };
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.action.value.fault == CHECK_FAULT_NONE &&
            (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_CONVERSION) != 0, "fabs keeps a wider integer conversion typed without synthesizing constant faults");
        integer = (CheckValue){ .type = type_atomic(table, formats[target]), .category = CHECK_LVALUE };
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.warnings == 0 &&
            (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "fabs retains an atomic source load and Clang's warning suppression for its original type");
        integer = check_test_float(formats[target], -2);
        integer.flags |= BUSTER_CHECK_SIDE_EFFECT;
        integer.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
        integer.fault_node = 41;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, !result.folded && result.evaluated_operands == 1 && result.action.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO &&
            result.action.value.fault_node == 41 && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "fabs preserves its sole operand's effects and fault");
        integer = check_test_float(BUSTER_TYPE_FLOAT128, -2);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.action.value.fault == CHECK_FAULT_NONE &&
            result.action.value.type == formats[target] && (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_CONVERSION) != 0,
            "fabs wider conversion is typed and explicitly unsupported without host approximation");
        integer.type = type_complex(table, BUSTER_TYPE_DOUBLE);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.action.value.type == formats[target] &&
            (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_CONVERSION) != 0 && (result.warnings & BUSTER_CHECK_BUILTIN_FLOAT_WARN_ABSOLUTE_VALUE) != 0,
            "fabs complex argument requires real-component conversion rather than complex magnitude");
        integer = check_test_integer(type_pointer(table, BUSTER_TYPE_INT), 0);
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED, "fabs does not accept a pointer as an FP prototype argument");
        action.arguments = NULL;
        action.argument_count = 0;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "fabs checks zero arity before reading its argument slice");
    }
    CheckValue signaling = { .type = BUSTER_TYPE_LONG_DOUBLE, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_FLOATING };
    const uint64_t significand = UINT64_C(0x8000000000000123);
    const uint16_t negative = 0xffff;
    memcpy(&signaling.floating, &significand, sizeof(significand));
    memcpy((uint8_t*)&signaling.floating + 8, &negative, sizeof(negative));
    const CheckBuiltinFloatAction action = { .id = CBUILTIN_FABSL, .arguments = &signaling, .argument_count = 1 };
    CheckBuiltinFloatResult result;
    check_builtin_float_one(table, &action, &result);
    uint64_t stored = 0;
    uint16_t exponent = 0;
    memcpy(&stored, &result.action.value.floating, sizeof(stored));
    memcpy(&exponent, (uint8_t*)&result.action.value.floating + 8, sizeof(exponent));
    check_builtin_float_test_expect(test, result.folded && stored == significand && exponent == 0x7fff, "same-format fabsl clears only a signaling NaN's sign bit");
}

static void check_builtin_float_test_constraints(CheckBuiltinFloatTest* test, TypeTable* table)
{
    CheckValue value = check_test_integer(BUSTER_TYPE_INT, 1);
    CheckBuiltinFloatAction action = {
        .id = CBUILTIN_ISNAN,
        .arguments = &value,
        .argument_count = 1,
    };
    CheckBuiltinFloatResult result;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "generic classification requires a real floating argument");
    value = check_test_float(BUSTER_TYPE_FLOAT128, 1);
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.floating_type == BUSTER_TYPE_FLOAT128 &&
                                    result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.runtime_builtin == CBUILTIN_ISNAN &&
                                    (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_FORMAT) != 0,
                                    "unsupported real format remains typed and is never approximated through host long double");
    value.type = type_complex(table, BUSTER_TYPE_DOUBLE);
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "generic classification rejects complex input");
    value = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    value.flags |= BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, !result.folded && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                                    (result.action.value.flags & BUSTER_CHECK_ICE) == 0, "classification does not discard an evaluated argument effect");
    for (uint32_t count = 0; count <= 7; count += 1)
    {
        action.argument_count = count;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, (result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT) == (count != 1), "unary classifier arity is bounded before reading arguments");
    }
    action.argument_count = 1;
    action.arguments = NULL;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonempty null argument slice is rejected");
    action.id = CBUILTIN_VA_START;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED, "foreign builtin ID cannot alias a floating family");
    CheckValue arguments[6] = {
        check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_INT, 2), check_test_integer(BUSTER_TYPE_INT, 3),
        check_test_integer(BUSTER_TYPE_INT, 4), check_test_integer(BUSTER_TYPE_INT, 5), check_test_float(BUSTER_TYPE_DOUBLE, 0),
    };
    const TypeId late = type_tag(table, BUSTER_TYPE_KIND_ENUM, 14890);
    check_builtin_float_test_expect(test, type_enum_complete(table, late, BUSTER_TYPE_INT, 0), "late selector enum fixture");
    const uint32_t tag = table->types.index[type_index(late)];
    uint32_t completions[512] = { 0 };
    completions[tag] = 900;
    arguments[0].type = late;
    action = (CheckBuiltinFloatAction){
        .id = CBUILTIN_FPCLASSIFY,
        .arguments = arguments,
        .argument_count = 6,
        .tag_completions = completions,
        .tag_completion_count = BUSTER_ARRAY_LENGTH(completions),
        .source_position = 899,
    };
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE && result.action.diagnostic_operand == 0,
                                    "even an unselected selector is typechecked at its own source-time completeness cutoff");
    action.source_position = 900;
    check_builtin_float_one(table, &action, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.folded, "completed selector enum becomes usable after its source cutoff");
}

// A value of `type`, as a constant of that type holds it.
static long double check_builtin_float_test_round(TypeId type, long double value)
{
    return type == BUSTER_TYPE_FLOAT ? (long double)(float)value : type == BUSTER_TYPE_DOUBLE ? (long double)(double)value : value;
}

// isgreater ... isunordered: their argument rules, their conversions to the common real floating type, and their folding
// against IEEE's rule stated apart (a NaN on either side makes all but isunordered false), for every pair of formats.
static void check_builtin_float_test_comparisons(CheckBuiltinFloatTest* test, TypeTable* table)
{
    const CBuiltinId ids[] = { CBUILTIN_ISGREATER, CBUILTIN_ISGREATEREQUAL, CBUILTIN_ISLESS, CBUILTIN_ISLESSEQUAL, CBUILTIN_ISLESSGREATER, CBUILTIN_ISUNORDERED };
    const long double values[] = { -__builtin_infl(), -1.5L, -0.0L, 0.0L, 0x1p-149L, 1.0L, 16777217.0L, 1e300L, __builtin_infl(), __builtin_nanl("") };
    const TypeId formats[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE };
    for (uint32_t id = 0; id < BUSTER_ARRAY_LENGTH(ids); id += 1)
    {
        for (uint32_t pair = 0; pair < BUSTER_ARRAY_LENGTH(formats) * BUSTER_ARRAY_LENGTH(formats); pair += 1)
        {
            const TypeId left = formats[pair / BUSTER_ARRAY_LENGTH(formats)];
            const TypeId right = formats[pair % BUSTER_ARRAY_LENGTH(formats)];
            const TypeId common = left == BUSTER_TYPE_LONG_DOUBLE || right == BUSTER_TYPE_LONG_DOUBLE ? BUSTER_TYPE_LONG_DOUBLE :
                                  left == BUSTER_TYPE_DOUBLE || right == BUSTER_TYPE_DOUBLE ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_FLOAT;
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(values) * BUSTER_ARRAY_LENGTH(values); v += 1)
            {
                const long double x = check_builtin_float_test_round(left, values[v / BUSTER_ARRAY_LENGTH(values)]);
                const long double y = check_builtin_float_test_round(right, values[v % BUSTER_ARRAY_LENGTH(values)]);
                const CheckValue arguments[2] = { check_test_float(left, x), check_test_float(right, y) };
                const CheckBuiltinFloatAction action = { .id = ids[id], .arguments = arguments, .argument_count = 2 };
                CheckBuiltinFloatResult result;
                check_builtin_float_one(table, &action, &result);
                const bool unordered = x != x || y != y;
                const bool expected = ids[id] == CBUILTIN_ISGREATER ? !unordered && x > y : ids[id] == CBUILTIN_ISGREATEREQUAL ? !unordered && x >= y :
                                      ids[id] == CBUILTIN_ISLESS ? !unordered && x < y : ids[id] == CBUILTIN_ISLESSEQUAL ? !unordered && x <= y :
                                      ids[id] == CBUILTIN_ISLESSGREATER ? !unordered && (x < y || y < x) : unordered;
                check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_INT &&
                                                result.action.value.constant_kind == CHECK_CONSTANT_INTEGER && result.action.value.integer == expected &&
                                                (result.action.value.flags & BUSTER_CHECK_ICE) != 0 && result.folded && result.runtime_builtin == CBUILTIN_NONE &&
                                                result.floating_type == common && result.argument_conversions[0].type == common &&
                                                result.argument_conversions[1].type == common,
                                                "a comparison of two constants folds to IEEE's unordered rule in their common format");
            }
        }
    }
    // The usual arithmetic conversions first: an int converts to float, rounded (16777217 is 16777216.0f).
    const CheckValue mixed[2] = { check_test_integer(BUSTER_TYPE_INT, 16777217), check_test_float(BUSTER_TYPE_FLOAT, 16777216.0L) };
    const CBuiltinId mixed_ids[] = { CBUILTIN_ISGREATER, CBUILTIN_ISGREATEREQUAL, CBUILTIN_ISLESSGREATER };
    const bool mixed_answers[] = { false, true, false };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(mixed_ids); i += 1)
    {
        const CheckBuiltinFloatAction action = { .id = mixed_ids[i], .arguments = mixed, .argument_count = 2 };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.floating_type == BUSTER_TYPE_FLOAT &&
                                        result.action.value.integer == mixed_answers[i] && result.argument_conversions[0].type == BUSTER_TYPE_FLOAT &&
                                        (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ARITHMETIC) != 0,
                                        "an integer argument converts to the other's floating type before the comparison");
    }
    // clang's rule: the conversions must give a real floating type, or "ordered compare requires two args of floating
    // point type", at the first argument.
    CheckValue invalid[2] = { check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_CHAR, 2) };
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_DOUBLE);
    const TypeId complex = type_complex(table, BUSTER_TYPE_DOUBLE);
    for (uint32_t i = 0; i < 4; i += 1)
    {
        if (i == 1)
        {
            invalid[0] = (CheckValue){ .type = pointer, .category = CHECK_RVALUE };
            invalid[1] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
        }
        else if (i == 2)
        {
            invalid[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
            invalid[0].type = complex;
        }
        else if (i == 3)
        {
            invalid[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
            invalid[1] = (CheckValue){ .type = pointer, .category = CHECK_RVALUE };
        }
        const CheckBuiltinFloatAction action = { .id = CBUILTIN_ISLESS, .arguments = invalid, .argument_count = 2 };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.action.diagnostic_operand == 0 &&
                                        !result.folded && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
                                        "two integers, a pointer or a complex argument is refused at the first argument");
    }
    const CheckValue pair[3] = { check_test_float(BUSTER_TYPE_DOUBLE, 1), check_test_float(BUSTER_TYPE_DOUBLE, 2), check_test_float(BUSTER_TYPE_DOUBLE, 3) };
    for (uint32_t count = 0; count <= 3; count += 1)
    {
        const CheckBuiltinFloatAction action = { .id = CBUILTIN_ISUNORDERED, .arguments = pair, .argument_count = count };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, (result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT) == (count != 2), "a comparison takes two arguments");
    }
    // An argument with an effect, or one that isn't constant, leaves the comparison to run: not folded, no ICE.
    CheckValue effects[2] = { check_test_float(BUSTER_TYPE_DOUBLE, 1), check_test_float(BUSTER_TYPE_FLOAT, 2) };
    effects[1].flags |= BUSTER_CHECK_SIDE_EFFECT;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (i == 1)
        {
            effects[1] = (CheckValue){ .type = BUSTER_TYPE_FLOAT, .category = CHECK_RVALUE };
        }
        const CheckBuiltinFloatAction action = { .id = CBUILTIN_ISLESSGREATER, .arguments = effects, .argument_count = 2 };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.runtime_builtin == CBUILTIN_ISLESSGREATER &&
                                        (result.action.value.flags & BUSTER_CHECK_ICE) == 0 && result.evaluated_operands == 3 &&
                                        ((result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0) == (i == 0) && result.floating_type == BUSTER_TYPE_DOUBLE,
                                        "a comparison with an effect or a value not constant runs, both arguments evaluated");
    }
    const CheckValue wide[2] = { check_test_float(BUSTER_TYPE_FLOAT128, 1), check_test_float(BUSTER_TYPE_DOUBLE, 2) };
    const CheckBuiltinFloatAction quad = { .id = CBUILTIN_ISGREATER, .arguments = wide, .argument_count = 2 };
    CheckBuiltinFloatResult result;
    check_builtin_float_one(table, &quad, &result);
    check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.floating_type == BUSTER_TYPE_FLOAT128 &&
                                    (result.unsupported & BUSTER_CHECK_BUILTIN_FLOAT_UNSUPPORTED_FORMAT) != 0,
                                    "a comparison in an unsupported format stays typed and unfolded");
}

static void check_builtin_float_test_batches(CheckBuiltinFloatTest* test, TypeTable* table)
{
    CheckValue values[6] = {
        check_test_integer(BUSTER_TYPE_INT, 1), check_test_integer(BUSTER_TYPE_INT, 2), check_test_integer(BUSTER_TYPE_INT, 3),
        check_test_integer(BUSTER_TYPE_INT, 4), check_test_integer(BUSTER_TYPE_INT, 5), check_test_float(BUSTER_TYPE_FLOAT, 1),
    };
    CheckBuiltinFloatAction actions[33];
    CheckBuiltinFloatResult whole[33];
    CheckBuiltinFloatResult split[33];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
    {
        actions[i] = (CheckBuiltinFloatAction){
            .id = i % 5 == 0 ? CBUILTIN_INF : i % 5 == 1 ? CBUILTIN_FPCLASSIFY : i % 5 == 2 ? CBUILTIN_ISNAN : i % 5 == 3 ? CBUILTIN_ISINF_SIGN : CBUILTIN_FABSF,
            .arguments = i % 5 == 1 ? values : values + 5,
            .argument_count = i % 5 == 0 ? 0 : i % 5 == 1 ? 6 : 1,
            .node = i,
        };
    }
    check_builtin_float(table, actions, whole, BUSTER_ARRAY_LENGTH(actions));
    for (uint32_t partition = 1; partition <= 9; partition += 1)
    {
        for (uint32_t first = 0; first < BUSTER_ARRAY_LENGTH(actions);)
        {
            const uint32_t count = BUSTER_ARRAY_LENGTH(actions) - first < partition ? BUSTER_ARRAY_LENGTH(actions) - first : partition;
            check_builtin_float(table, actions + first, split + first, count);
            first += count;
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(actions); i += 1)
        {
            check_builtin_float_test_expect(test, check_builtin_float_test_equal(whole + i, split + i), "legal floating-action partition changed semantic/evaluation metadata");
        }
    }
    check_builtin_float(NULL, NULL, NULL, 0);
}

static void check_builtin_float_test_pages(CheckBuiltinFloatTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_bytes = 17 * sizeof(CheckBuiltinFloatAction);
    const size_t output_bytes = 17 * sizeof(CheckBuiltinFloatResult);
    const size_t input_region = page != 0 ? ((input_bytes + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((output_bytes + 16 + page - 1) / page) * page : 0;
    const size_t size = input_region + output_region + 4 * page;
    uint8_t* mapping = page != 0 ? mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* payload_end = ready ? mapping + input_region + output_region + 3 * page : NULL;
    if (ready)
    {
        ready = mprotect(mapping + input_region, page, PROT_NONE) == 0 &&
                mprotect(mapping + input_region + page + output_region, page, PROT_NONE) == 0 &&
                mprotect(payload_end, page, PROT_NONE) == 0;
    }
    check_builtin_float_test_expect(test, ready, "guarded floating-action/payload allocation");
    const CheckValue string = {
        .type = type_array(table, BUSTER_TYPE_CHAR, 34),
        .category = CHECK_LVALUE,
        .flags = BUSTER_CHECK_STATIC_OBJECT,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = 81,
    };
    for (uint32_t length = 0; ready && length <= 33; length += 1)
    {
        uint8_t* bytes = payload_end - length;
        memset(bytes - 16, 0xa5, 16);
        memset(bytes, '7', length);
        const CheckBuiltinFloatAction action = {
            .id = CBUILTIN_NANL,
            .arguments = &string,
            .argument_count = 1,
            .nan_payload = bytes,
            .nan_payload_length = length,
            .nan_payload_known = true,
        };
        CheckBuiltinFloatResult result;
        check_builtin_float_one(table, &action, &result);
        check_builtin_float_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.folded,
                                        "payload at a guarded logical end needs neither terminator nor readable padding");
    }
    for (uint32_t count = 0; ready && count <= 17; count += 1)
    {
        CheckBuiltinFloatAction* actions = (CheckBuiltinFloatAction*)(mapping + input_region - count * sizeof(*actions));
        CheckBuiltinFloatResult* results = (CheckBuiltinFloatResult*)(mapping + input_region + page + output_region - count * sizeof(*results));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        for (uint32_t i = 0; i < count; i += 1)
        {
            actions[i] = (CheckBuiltinFloatAction){
                .id = i % 2 == 0 ? CBUILTIN_INF : CBUILTIN_NAN,
                .arguments = &string,
                .argument_count = i % 2,
                .nan_payload = payload_end - 33,
                .nan_payload_length = 33,
                .nan_payload_known = true,
            };
        }
        check_builtin_float(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_float_test_expect(test, results[i].action.diagnostic == CHECK_DIAG_NONE && results[i].folded,
                                            "guarded record tail preserves valid constant actions");
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_float_test_expect(test, *((uint8_t*)actions - i) == 0xa5 && *((uint8_t*)results - i) == 0xa5,
                                            "floating action wrote before its logical input/output slice");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, size);
    }
}

static bool check_builtin_float_test_clang_run(const char* clang, const char* path, bool* rejected)
{
    // parse_test_clang deliberately supplies -w. These cases also test pragma-upgraded warnings, which -w would
    // suppress even after an error mapping; use an explicit diagnostic-preserving boundary for the source oracle.
    char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-color-diagnostics", (char*)path, NULL };
    posix_spawn_file_actions_t actions;
    bool ready = posix_spawn_file_actions_init(&actions) == 0;
    const bool initialized = ready;
    if (ready)
    {
        ready = posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) == 0 &&
                posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0;
    }
    pid_t child = -1;
    if (ready)
    {
        ready = posix_spawnp(&child, clang, &actions, NULL, arguments, environ) == 0;
    }
    if (initialized)
    {
        posix_spawn_file_actions_destroy(&actions);
    }
    int status = 0;
    if (ready)
    {
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        ready = waited == child && WIFEXITED(status);
    }
    *rejected = ready && WEXITSTATUS(status) != 0;
    return ready;
}

static void check_builtin_float_test_oracle(CheckBuiltinFloatTest* test, const char* clang)
{
    const char* sources[] = {
        "_Static_assert(_Generic(__builtin_huge_valf(),float:1,default:0),\"float result\");\n"
        "_Static_assert(_Generic(__builtin_huge_val(),double:1,default:0),\"double result\");\n"
        "_Static_assert(_Generic(__builtin_huge_vall(),long double:1,default:0),\"long double result\");\n",
        "_Static_assert(__builtin_isinf(__builtin_inf()),\"inf\");\n"
        "_Static_assert(__builtin_isnan(__builtin_nan(\"0x123\")),\"nan\");\n"
        "_Static_assert(__builtin_signbit(-0.0),\"negative zero\");\n"
        "_Static_assert(__builtin_isfinite(0x1p-149f),\"subnormal finite\");\n",
        "int f(void);\n_Static_assert(__builtin_fpclassify(f(),22,33,44,55,0.0)==55,\"only selected constexpr selector\");\n"
        "_Static_assert(__builtin_fpclassify(11,22,33,44,55,0x1p-149f)==44,\"source float subnormal\");\n"
        "_Static_assert(__builtin_fpclassify(11.75,22,33,44,55,__builtin_nan(\"\"))==11,\"selector conversion\");\n",
        "int f(void) { return __builtin_isnan(1); }\n",
        "int f(void) { return __builtin_isinf(1.0+2.0i); }\n",
        "double f(void) { return __builtin_inf(1); }\n",
        "int f(void) { return __builtin_fpclassify(1,2,3,4,5); }\n",
        "double f(const char*p) { return __builtin_nan(p); }\n",
        "double f(void) { return __builtin_nan(\"bad\"); }\n",
        "double x=__builtin_nan(\"bad\");\n",
        "double x=__builtin_nan(\"1\\0two\");\n",
        "double x=__builtin_nan(\"340282366920938463463374607431768211456123456789\");\n",
        "int f(__float128 x) { return __builtin_isfinite(x); }\n",
        "_Static_assert(_Generic(__builtin_fabs(-3),double:1,default:0),\"double\");\n"
        "_Static_assert(_Generic(__builtin_fabsf(2),float:1,default:0),\"float\");\n"
        "_Static_assert(_Generic(__builtin_fabsl(2),long double:1,default:0),\"long double\");\n"
        "double d=__builtin_fabs(-3); float f=__builtin_fabsf(-2.25); long double l=__builtin_fabsl(-2.0L);\n",
        "enum { A=__builtin_isinf_sign(-__builtin_inf()), B=__builtin_isinf_sign(__builtin_inf()), C=__builtin_isinf_sign(-0.0), D=__builtin_isinf_sign(__builtin_nan(\"\")) };\n"
        "_Static_assert(A==-1 && B==1 && C==0 && D==0,\"signed infinity classification\");\n",
        "int n=__builtin_choose_expr((int)__builtin_fabs(-2.0),1,2);\n",
        "int n=__builtin_choose_expr(0&&__builtin_fabs(-2.0),1,2);\n",
        "double x; int n=__builtin_choose_expr(sizeof(__builtin_fabs(x))==sizeof(double),1,2);\n",
        "double x=__builtin_fabs(3.0+4.0i);\n",
        "int n=__builtin_choose_expr(__builtin_isinf_sign(__builtin_fabs(-__builtin_inf()))==1,1,2);\n",
        "#pragma clang diagnostic error \"-Wabsolute-value\"\ndouble f(int x) { return __builtin_fabs(x); }\n",
        "#pragma clang diagnostic error \"-Wabsolute-value\"\nfloat f(double x) { return __builtin_fabsf(x); }\n",
        "#pragma clang diagnostic error \"-Wabsolute-value\"\ndouble f(__int128 x) { return __builtin_fabs(x); }\n",
        "#pragma clang diagnostic error \"-Wabsolute-value\"\ndouble f(_Atomic(double)*p) { return __builtin_fabs(*p); }\n",
        "#pragma clang diagnostic error \"-Wabsolute-value\"\nenum E : unsigned __int128 { A }; double f(enum E x) { return __builtin_fabs(x); }\n",
        "double f(void*p) { return __builtin_fabs(p); }\n",
        "int f(int x) { return __builtin_isinf_sign(x); }\n",
        "_Static_assert(__builtin_isgreater(2.0,1.0) && !__builtin_isgreater(__builtin_nan(\"\"),1.0) && __builtin_isunordered(__builtin_nan(\"\"),1.0f) &&"
        " __builtin_islessgreater(1.0f,2.0L) && !__builtin_islessgreater(1.0,1) && __builtin_isgreaterequal(16777216.0f,16777217) &&"
        " !__builtin_isgreater(16777216.0f,16777217) && __builtin_islessequal(-0.0,0.0) && !__builtin_isless(-0.0,0.0),\"comparisons\");\n"
        "int a[__builtin_isless(1,2.0)]; int x=__builtin_isunordered(1.0L,2);\n",
        "int f(int a, int b) { return __builtin_isless(a, b); }\n",
        "int f(double a) { return __builtin_isless(a); }\n",
        "int f(double a) { return __builtin_isless(a, a, a); }\n",
        "int f(double *p, double a) { return __builtin_isgreater(p, a); }\n",
        "int f(_Complex double z, double a) { return __builtin_isunordered(z, a); }\n",
        "int f(_Bool b, char c) { return __builtin_islessequal(b, c); }\n",
    };
    const bool rejected[] = { false, false, false, true, true, true, true, false, false, true, true, false, false,
                             false, false, true, true, false, false, false, true, true, false, false, true, true, true,
                             false, true, true, true, true, true, true };
    _Static_assert(BUSTER_ARRAY_LENGTH(sources) == BUSTER_ARRAY_LENGTH(rejected), "every floating oracle source has an expected result");
    char directory[] = "/tmp/buster-check-builtin-float-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        ready = parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        bool failed = false;
        ready = check_builtin_float_test_clang_run(clang, paths[i], &failed);
        check_builtin_float_test_expect(test, ready && failed == rejected[i], "Clang disagrees with a floating builtin source constraint/constant/warning fixture");
    }
    check_builtin_float_test_expect(test, ready, "independent Clang floating constraint/constant oracle could not run");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static int run_check_builtin_float_tests(int argc, char** argv)
{
    CheckBuiltinFloatTest test = { 0 };
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
    check_builtin_float_test_expect(&test, options, "usage: test-check-builtin-float [--clang PATH]");
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_float_test_expect(&test, initialized, "floating action type-table allocation");
        if (initialized)
        {
            check_builtin_float_test_constants(&test, &types.table);
            check_builtin_float_test_classification(&test, &types.table);
            check_builtin_float_test_absolute(&test, &types.table);
            check_builtin_float_test_constraints(&test, &types.table);
            check_builtin_float_test_comparisons(&test, &types.table);
            check_builtin_float_test_batches(&test, &types.table);
            check_builtin_float_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_float_test_oracle(&test, clang);
    }
    printf("test-check-builtin-float: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
