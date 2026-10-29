// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original typed math fixtures for #148. Include after c_checker_test.c, c_check_builtin_float_test.c and the math
// action module. Native Clang copysign is the independent numeric oracle. The other twelve operations deliberately
// have no invented source constant; separate Clang source fixtures test that distinction, types and constraints.
// Existing type fixtures, exact floating comparison and diagnostic-preserving process boundaries are reused.

typedef struct CheckBuiltinMathTest CheckBuiltinMathTest;
struct CheckBuiltinMathTest
{
    uint64_t checks;
    uint64_t failures;
};

static const CBuiltinId check_builtin_math_test_ids[] =
{
    CBUILTIN_SQRT,
    CBUILTIN_SQRTF,
    CBUILTIN_COPYSIGN,
    CBUILTIN_COPYSIGNF,
    CBUILTIN_CEIL,
    CBUILTIN_CEILF,
    CBUILTIN_FLOOR,
    CBUILTIN_FLOORF,
    CBUILTIN_TRUNC,
    CBUILTIN_TRUNCF,
    CBUILTIN_NEARBYINT,
    CBUILTIN_NEARBYINTF,
    CBUILTIN_RINT,
    CBUILTIN_RINTF,
};

static void check_builtin_math_test_expect(CheckBuiltinMathTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-check-builtin-math: %s\n", description);
        }
    }
}

static bool check_builtin_math_test_equal(const CheckBuiltinMathResult* a, const CheckBuiltinMathResult* b)
{
    bool equal = check_test_action_equal(&a->action, &b->action) && a->floating_type == b->floating_type &&
                 a->evaluated_operands == b->evaluated_operands && a->unsupported == b->unsupported && a->runtime_builtin == b->runtime_builtin &&
                 a->folded == b->folded && a->compiler_barrier == b->compiler_barrier && a->reads_fp_environment == b->reads_fp_environment &&
                 a->may_raise_fp_exceptions == b->may_raise_fp_exceptions && a->may_raise_inexact == b->may_raise_inexact && a->may_set_errno == b->may_set_errno;
    for (uint32_t i = 0; equal && i < 2; i += 1)
    {
        equal = check_test_value_equal(a->converted_arguments + i, b->converted_arguments + i) &&
                a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return equal;
}

static void check_builtin_math_test_values(CheckBuiltinMathTest* test, TypeTable* table)
{
    CheckValue values[2] = { check_test_integer(BUSTER_TYPE_INT, (uint32_t)-9), check_test_float(BUSTER_TYPE_DOUBLE, -0.0L) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_math_test_ids); i += 1)
    {
        const CBuiltinId id = check_builtin_math_test_ids[i];
        const CheckBuiltinMathKind kind = check_builtin_math_kind(id);
        const bool copy = kind == CHECK_BUILTIN_MATH_COPYSIGN;
        const TypeId type = i % 2 == 0 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_FLOAT;
        const CheckBuiltinMathAction action = { .id = id, .arguments = values, .argument_count = copy ? 2 : 1 };
        CheckBuiltinMathResult result;
        check_builtin_math_one(table, &action, &result);
        check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == type && result.floating_type == type &&
            result.action.value.category == CHECK_RVALUE && result.evaluated_operands == (copy ? 3u : 1u) && result.unsupported == 0 &&
            result.argument_conversions[0].type == type && result.converted_arguments[0].type == type &&
            (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0, "every confirmed name has its ordinary declared FP prototype");
        check_builtin_math_test_expect(test, (result.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL)) == 0,
            "a floating math call never gains integer/literal constant-expression provenance");
        if (copy)
        {
            check_builtin_math_test_expect(test, result.folded && result.action.value.constant_kind == CHECK_CONSTANT_FLOATING && result.action.value.floating == -9 &&
                result.runtime_builtin == CBUILTIN_NONE && !result.compiler_barrier && !result.reads_fp_environment && !result.may_raise_fp_exceptions && !result.may_set_errno &&
                result.argument_conversions[1].type == type && result.converted_arguments[1].type == type, "copysign alone folds without an operation environment/errno effect");
        }
        else
        {
            check_builtin_math_test_expect(test, !result.folded && result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.runtime_builtin == id &&
                result.compiler_barrier && result.may_raise_fp_exceptions && result.reads_fp_environment && result.action.value.fault == CHECK_FAULT_NONE &&
                result.may_set_errno == (kind == CHECK_BUILTIN_MATH_SQRT) && result.may_raise_inexact == (kind != CHECK_BUILTIN_MATH_NEARBYINT) &&
                (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "runtime math keeps errno, rounding and exception requirements without a fabricated AST fold");
        }
    }
    const long double magnitudes[] = { 0, -0.0L, 1.5L, -1.5L, 0x1p-149L, 0x1p-1074L, __builtin_infl(), -__builtin_infl(), __builtin_nanl("123"), -__builtin_nanl("123") };
    const long double signs[] = { 0, -0.0L, 1, -1, __builtin_nanl("9"), -__builtin_nanl("9") };
    for (uint32_t format = 0; format < 2; format += 1)
    {
        const TypeId type = format == 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(magnitudes); a += 1)
        {
            for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(signs); b += 1)
            {
                values[0] = check_test_float(type, check_action_round_float(type, magnitudes[a]));
                values[1] = check_test_float(type, check_action_round_float(type, signs[b]));
                const CheckBuiltinMathAction action = { .id = format == 0 ? CBUILTIN_COPYSIGNF : CBUILTIN_COPYSIGN, .arguments = values, .argument_count = 2 };
                CheckBuiltinMathResult result;
                check_builtin_math_one(table, &action, &result);
                const long double expected = format == 0 ? __builtin_copysignf((float)values[0].floating, (float)values[1].floating) :
                    __builtin_copysign((double)values[0].floating, (double)values[1].floating);
                check_builtin_math_test_expect(test, result.folded && check_builtin_float_test_bits(type, result.action.value.floating, expected),
                    "copysign representation matches native Clang for signs, zeros, subnormals, infinities and NaN payloads");
            }
        }
        // Build the represented source sNaN without passing it through a host FP conversion or a numeric oracle.
        // Its payload is aligned from source precision into CheckValue's x87 significand; only the sign may change.
        values[0] = (CheckValue){ .type = type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_FLOATING };
        values[1] = check_test_float(type, -0.0L);
        const uint64_t significand = UINT64_C(0x8000000000000000) | (UINT64_C(0x123) << (format == 0 ? 40 : 11));
        const uint16_t positive = 0x7fff;
        memcpy(&values[0].floating, &significand, sizeof(significand));
        memcpy((uint8_t*)&values[0].floating + 8, &positive, sizeof(positive));
        const CheckBuiltinMathAction action = { .id = format == 0 ? CBUILTIN_COPYSIGNF : CBUILTIN_COPYSIGN, .arguments = values, .argument_count = 2 };
        CheckBuiltinMathResult result;
        check_builtin_math_one(table, &action, &result);
        uint64_t stored = 0;
        uint16_t exponent = 0;
        memcpy(&stored, &result.action.value.floating, sizeof(stored));
        memcpy(&exponent, (uint8_t*)&result.action.value.floating + 8, sizeof(exponent));
        check_builtin_math_test_expect(test, result.folded && stored == significand && exponent == 0xffff,
            "same-format copysign preserves a signaling NaN's payload and quiet bit while copying a negative-zero sign");
    }
    values[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    values[1] = check_test_float(BUSTER_TYPE_DOUBLE, -1);
    values[1].flags |= BUSTER_CHECK_SIDE_EFFECT;
    CheckBuiltinMathAction action = { .id = CBUILTIN_COPYSIGN, .arguments = values, .argument_count = 2 };
    CheckBuiltinMathResult result;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, !result.folded && result.runtime_builtin == CBUILTIN_COPYSIGN && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
        (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && result.evaluated_operands == 3 &&
        result.converted_arguments[0].constant_kind == CHECK_CONSTANT_FLOATING && result.converted_arguments[1].constant_kind == CHECK_CONSTANT_FLOATING,
        "effectful copysign remains runtime and cannot masquerade as a source constant initializer");
    values[1] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    values[0] = check_test_float(BUSTER_TYPE_DOUBLE, 1);
    values[0].flags |= BUSTER_CHECK_SIDE_EFFECT;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, !result.folded && result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.evaluated_operands == 3,
        "copysign comma-expression effects forbid source constant initialization on either operand");
    values[0].fault = CHECK_FAULT_SHIFT_COUNT;
    values[0].fault_node = 17;
    values[1].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    values[1].fault_node = 29;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, !result.folded && result.action.value.fault == CHECK_FAULT_SHIFT_COUNT && result.action.value.fault_node == 17,
        "math retains operand faults without replacing them with a host math-domain result");
}

static void check_builtin_math_test_constraints(CheckBuiltinMathTest* test, TypeTable* table)
{
    CheckValue values[2] = { check_test_float(BUSTER_TYPE_FLOAT128, 2), check_test_float(BUSTER_TYPE_DOUBLE, -1) };
    CheckBuiltinMathAction action = { .id = CBUILTIN_COPYSIGN, .arguments = values, .argument_count = 2 };
    CheckBuiltinMathResult result;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_DOUBLE && !result.folded &&
        result.action.value.fault == CHECK_FAULT_NONE && (result.unsupported & BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_CONVERSION) != 0,
        "wider source conversions remain typed runtime requirements without host approximation");
    values[0].type = type_complex(table, BUSTER_TYPE_DOUBLE);
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.converted_arguments[0].type == BUSTER_TYPE_DOUBLE &&
        (result.unsupported & BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_CONVERSION) != 0, "complex math arguments require real-component prototype conversion");
    values[0] = (CheckValue){ .type = type_bit_int(table, 256, false), .category = CHECK_RVALUE };
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.fault == CHECK_FAULT_NONE &&
        (result.unsupported & BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_CONVERSION) != 0, "wider integer conversion has explicit unsupported metadata");
    values[0] = (CheckValue){ .type = type_pointer(table, BUSTER_TYPE_INT), .category = CHECK_RVALUE };
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED, "math FP prototypes reject pointer arguments");
    values[0] = (CheckValue){ .type = type_atomic(table, BUSTER_TYPE_FLOAT), .category = CHECK_LVALUE };
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded &&
        (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0,
        "math retains an atomic operand's ordinary load effect and conversion");
    values[0] = check_test_float(BUSTER_TYPE_DOUBLE, 0x1.000001p0L);
    values[1] = check_test_float(BUSTER_TYPE_DOUBLE, -1);
    action.id = CBUILTIN_COPYSIGNF;
    action.nondefault_fp_environment = true;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.folded && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
        result.action.value.fault == CHECK_FAULT_NONE && (result.unsupported & BUSTER_CHECK_BUILTIN_MATH_UNSUPPORTED_FP_ENVIRONMENT) != 0 &&
        result.reads_fp_environment && result.may_raise_fp_exceptions && result.compiler_barrier && (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0,
        "unrepresented source FP modes retain conversion effects and never use host rounding to invent a source constant");
    values[0] = check_test_float(BUSTER_TYPE_FLOAT, 1);
    values[1] = check_test_float(BUSTER_TYPE_FLOAT, -1);
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.folded && result.unsupported == 0 && result.action.value.floating == -1,
        "identity-format copysign remains an exact source constant under nondefault FP options");
    action.id = CBUILTIN_COPYSIGN;
    action.nondefault_fp_environment = false;
    values[1] = check_test_float(BUSTER_TYPE_DOUBLE, -1);
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 14830);
    check_builtin_math_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0), "math source-history enum fixture");
    const uint32_t tag = table->types.index[type_index(enumeration)];
    uint32_t completions[512] = { 0 };
    completions[tag] = 90;
    values[0] = check_test_integer(enumeration, 7);
    action.tag_completions = completions;
    action.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
    action.source_position = 89;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "math conversion cannot observe an enum's future completion");
    action.source_position = 90;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.folded && result.action.value.floating == -7,
        "completed enum converts before exact sign copy");
    action.tag_completions = NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_math_test_ids); i += 1)
    {
        action.id = check_builtin_math_test_ids[i];
        const uint32_t expected = check_builtin_math_kind(action.id) == CHECK_BUILTIN_MATH_COPYSIGN ? 2 : 1;
        for (uint32_t count = 0; count < 4; count += 1)
        {
            action.argument_count = count;
            check_builtin_math_one(table, &action, &result);
            check_builtin_math_test_expect(test, (result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT) == (count != expected), "math checks exact arity before reading arguments");
        }
    }
    action = (CheckBuiltinMathAction){ .id = CBUILTIN_SQRT, .argument_count = 1 };
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "nonempty null math slice diagnoses");
    action.id = CBUILTIN_FABS;
    check_builtin_math_one(table, &action, &result);
    check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_UNSUPPORTED, "math IDs do not alias another frozen semantic family");
}

static void check_builtin_math_test_pages(CheckBuiltinMathTest* test, TypeTable* table)
{
    const uint64_t page = 4096;
    const uint64_t input_bytes = ((sizeof(CheckBuiltinMathAction) * 33 + 16 + page - 1) / page) * page;
    const uint64_t output_bytes = ((sizeof(CheckBuiltinMathResult) * 33 + 16 + page - 1) / page) * page;
    const uint64_t bytes = input_bytes + output_bytes + page * 2;
    uint8_t* memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = memory != MAP_FAILED;
    if (ready)
    {
        ready = mprotect(memory + input_bytes, page, PROT_NONE) == 0 && mprotect(memory + input_bytes + page + output_bytes, page, PROT_NONE) == 0;
    }
    check_builtin_math_test_expect(test, ready, "math guarded input/output allocation");
    CheckValue values[2] = { check_test_float(BUSTER_TYPE_DOUBLE, 1), check_test_float(BUSTER_TYPE_DOUBLE, -0.0L) };
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        CheckBuiltinMathAction* actions = (CheckBuiltinMathAction*)(memory + input_bytes - count * sizeof(*actions));
        CheckBuiltinMathResult* results = (CheckBuiltinMathResult*)(memory + input_bytes + page + output_bytes - count * sizeof(*results));
        memset((uint8_t*)actions - 16, 0xa5, count * sizeof(*actions) + 16);
        memset((uint8_t*)results - 16, 0xa5, count * sizeof(*results) + 16);
        for (uint32_t i = 0; i < count; i += 1)
        {
            actions[i] = (CheckBuiltinMathAction){ .id = check_builtin_math_test_ids[i % 14], .arguments = values,
                .argument_count = i % 14 == 2 || i % 14 == 3 ? 2 : 1 };
        }
        check_builtin_math(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_math_test_expect(test, results[i].action.diagnostic == CHECK_DIAG_NONE, "math guarded record tail is bounded");
        }
        for (uint32_t i = 1; i <= 16; i += 1)
        {
            check_builtin_math_test_expect(test, ((uint8_t*)actions)[-(int)i] == 0xa5 && ((uint8_t*)results)[-(int)i] == 0xa5,
                "math batch preserves bytes before its bounded slices");
        }
    }
    if (ready)
    {
        for (uint32_t count = 1; count <= 2; count += 1)
        {
            CheckValue* arguments = (CheckValue*)(memory + input_bytes - count * sizeof(*arguments));
            for (uint32_t i = 0; i < count; i += 1)
            {
                arguments[i] = check_test_float(BUSTER_TYPE_DOUBLE, i == 0 ? 2 : -1);
            }
            const CheckBuiltinMathAction action = { .id = count == 1 ? CBUILTIN_SQRT : CBUILTIN_COPYSIGN, .arguments = arguments, .argument_count = count };
            CheckBuiltinMathResult result;
            check_builtin_math_one(table, &action, &result);
            check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_operands == (count == 1 ? 1u : 3u),
                "math reads exactly its one- or two-element argument slice before a protected page");
        }
        const CheckBuiltinMathAction action = { .id = CBUILTIN_COPYSIGN, .arguments = (const CheckValue*)(memory + input_bytes), .argument_count = 3 };
        CheckBuiltinMathResult result;
        check_builtin_math_one(table, &action, &result);
        check_builtin_math_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "invalid math arity never reads inaccessible arguments");
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, bytes);
    }
    CheckBuiltinMathAction actions[33];
    CheckBuiltinMathResult whole[33];
    CheckBuiltinMathResult split[33];
    for (uint32_t i = 0; i < 33; i += 1)
    {
        actions[i] = (CheckBuiltinMathAction){ .id = check_builtin_math_test_ids[i % 14], .arguments = values,
            .argument_count = i % 14 == 2 || i % 14 == 3 ? 2 : 1 };
    }
    check_builtin_math(table, actions, whole, 33);
    for (uint32_t partition = 1; partition <= 9; partition += 1)
    {
        for (uint32_t first = 0; first < 33;)
        {
            const uint32_t count = 33 - first < partition ? 33 - first : partition;
            check_builtin_math(table, actions + first, split + first, count);
            first += count;
        }
        for (uint32_t i = 0; i < 33; i += 1)
        {
            check_builtin_math_test_expect(test, check_builtin_math_test_equal(whole + i, split + i), "math partition changed typed/fold/effect metadata");
        }
    }
    check_builtin_math(NULL, NULL, NULL, 0);
}

static void check_builtin_math_test_clang(CheckBuiltinMathTest* test, const char* clang)
{
    const char* names[] = { "sqrt", "sqrtf", "copysign", "copysignf", "ceil", "ceilf", "floor", "floorf", "trunc", "truncf", "nearbyint", "nearbyintf", "rint", "rintf" };
    char directory[] = "/tmp/buster-check-builtin-math-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char path[160];
    snprintf(path, sizeof(path), "%s/source.c", directory);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        const char* type = i % 2 == 0 ? "double" : "float";
        const bool copy = i == 2 || i == 3;
        char source[1024];
        snprintf(source, sizeof(source), "%s f(int x) { return __builtin_%s(x%s); }\n_Static_assert(_Generic(__builtin_%s(4%s),%s:1,default:0),\"declared math type\");\n",
            type, names[i], copy ? ", -1" : "", names[i], copy ? ", -1" : "", type);
        snprintf(path, sizeof(path), "%s/prototype-%u.c", directory, i);
        ready = parse_test_write(path, source, strlen(source));
        bool rejected = false;
        ready = ready && check_builtin_float_test_clang_run(clang, path, &rejected);
        check_builtin_math_test_expect(test, ready && !rejected, "Clang accepts each ordinary math prototype and its integer argument conversion");
        unlink(path);
        snprintf(source, sizeof(source), "%s x=__builtin_%s(4.0%s);\n", type, names[i], copy ? ", -0.0" : "");
        snprintf(path, sizeof(path), "%s/initializer-%u.c", directory, i);
        ready = ready && parse_test_write(path, source, strlen(source)) && check_builtin_float_test_clang_run(clang, path, &rejected);
        unlink(path);
        check_builtin_math_test_expect(test, ready && rejected == !copy, "Clang source initializers distinguish copysign folding from later optimizer math folding");
    }
    const char* sources[] =
    {
        "double x=__builtin_copysign(1.0, -0.0); float y=__builtin_copysignf(0.0f,-1.0f); enum { A=__builtin_signbit(__builtin_copysign(1.0,-0.0)) };\n",
        "int n=__builtin_choose_expr((int)__builtin_copysign(1.0,-0.0),1,2);\n",
        "double f(double _Complex z) { return __builtin_sqrt(z); } float g(double _Complex z) { return __builtin_copysignf(z,z); }\n",
        "double f(void*p) { return __builtin_sqrt(p); }\n",
        "double f(double x) { return __builtin_copysign(x); }\n",
        "double f(double x) { return __builtin_ceil(x,x); }\n",
        "#pragma STDC FENV_ACCESS ON\ndouble f(double x) { return __builtin_sqrt(x)+__builtin_rint(x)+__builtin_nearbyint(x); }\n",
        "int x; double y=__builtin_copysign((x++,1.0),1.0);\n",
        "#pragma STDC FENV_ACCESS ON\nfloat x=__builtin_copysignf(0x1.000001p0,-1.0);\n",
    };
    const bool rejected[] = { false, true, false, true, true, true, false, true, false };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        bool failed = false;
        snprintf(path, sizeof(path), "%s/constraint-%u.c", directory, i);
        ready = parse_test_write(path, sources[i], strlen(sources[i])) && check_builtin_float_test_clang_run(clang, path, &failed);
        unlink(path);
        check_builtin_math_test_expect(test, ready && failed == rejected[i], "Clang math constraint/constant/environment fixture disagrees");
    }
    check_builtin_math_test_expect(test, ready, "independent Clang math source oracle could not run");
    unlink(path);
    rmdir(directory);
}

static __attribute__((unused)) int run_check_builtin_math_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinMathTest test = { 0 };
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
    check_builtin_math_test_expect(&test, options, "usage: test-check-builtins-math [--clang PATH]");
    if (options)
    {
        CheckTestTypes types = { 0 };
        const bool initialized = check_test_types_open(&types);
        check_builtin_math_test_expect(&test, initialized, "math type-table allocation");
        if (initialized)
        {
            check_builtin_math_test_values(&test, &types.table);
            check_builtin_math_test_constraints(&test, &types.table);
            check_builtin_math_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_math_test_clang(&test, clang);
    }
    printf("test-check-builtins-math: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    oracle_cache_report(oracle_cache(), "test-check-builtins-math");
    oracle_epoch_end(epoch);
    return result;
}
