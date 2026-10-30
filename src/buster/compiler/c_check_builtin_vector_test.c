// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original fixtures for the 23 observed generic vector builtin spellings. Include after c_checker_test.c and
// c_check_builtin_vector.c. Independent expected TypeIds use the C type factories; no test invents a CheckValue
// vector constant representation. Clang 23.1.1 or newer in GNU17 runs externally when this command runs on the approved
// host. Sources: llvmorg-23.1.1 SemaChecking.cpp elementwise/reduce/shuffle/convert/nontemporal/nondeterministic,
// SemaExpr.cpp unary FP and initialization, Type.cpp/TypeBase.h enum/complex predicates, and CGBuiltin.cpp's
// unevaluated nondeterministic operand. These are behavior facts; no external implementation or tests are copied.

typedef struct CheckBuiltinVectorTest CheckBuiltinVectorTest;
struct CheckBuiltinVectorTest
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CheckBuiltinVectorTestCase CheckBuiltinVectorTestCase;
struct CheckBuiltinVectorTestCase
{
    CBuiltinId id;
    const char* name;
    uint32_t arity;
};

static const CheckBuiltinVectorTestCase check_builtin_vector_test_cases[] =
{
    { CBUILTIN_ELEMENTWISE_ABS, "__builtin_elementwise_abs", 1 },
    { CBUILTIN_ELEMENTWISE_ADD_SAT, "__builtin_elementwise_add_sat", 2 },
    { CBUILTIN_ELEMENTWISE_BITREVERSE, "__builtin_elementwise_bitreverse", 1 },
    { CBUILTIN_ELEMENTWISE_CLZG, "__builtin_elementwise_clzg", 1 },
    { CBUILTIN_ELEMENTWISE_FMA, "__builtin_elementwise_fma", 3 },
    { CBUILTIN_ELEMENTWISE_FSHL, "__builtin_elementwise_fshl", 3 },
    { CBUILTIN_ELEMENTWISE_FSHR, "__builtin_elementwise_fshr", 3 },
    { CBUILTIN_ELEMENTWISE_MAX, "__builtin_elementwise_max", 2 },
    { CBUILTIN_ELEMENTWISE_MIN, "__builtin_elementwise_min", 2 },
    { CBUILTIN_ELEMENTWISE_POPCOUNT, "__builtin_elementwise_popcount", 1 },
    { CBUILTIN_ELEMENTWISE_SQRT, "__builtin_elementwise_sqrt", 1 },
    { CBUILTIN_ELEMENTWISE_SUB_SAT, "__builtin_elementwise_sub_sat", 2 },
    { CBUILTIN_REDUCE_ADD, "__builtin_reduce_add", 1 },
    { CBUILTIN_REDUCE_AND, "__builtin_reduce_and", 1 },
    { CBUILTIN_REDUCE_MAX, "__builtin_reduce_max", 1 },
    { CBUILTIN_REDUCE_MIN, "__builtin_reduce_min", 1 },
    { CBUILTIN_REDUCE_MUL, "__builtin_reduce_mul", 1 },
    { CBUILTIN_REDUCE_OR, "__builtin_reduce_or", 1 },
    { CBUILTIN_SHUFFLEVECTOR, "__builtin_shufflevector", 2 },
    { CBUILTIN_CONVERTVECTOR, "__builtin_convertvector", 1 },
    { CBUILTIN_NONDETERMINISTIC_VALUE, "__builtin_nondeterministic_value", 1 },
    { CBUILTIN_NONTEMPORAL_LOAD, "__builtin_nontemporal_load", 1 },
    { CBUILTIN_NONTEMPORAL_STORE, "__builtin_nontemporal_store", 2 },
};

static void check_builtin_vector_test_expect(CheckBuiltinVectorTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 32)
        {
            fprintf(stderr, "test-check-builtins-vector: %s\n", description);
        }
    }
}

static CheckValue check_builtin_vector_test_value(TypeId type)
{
    const CheckValue result = { .type = type, .category = CHECK_RVALUE };
    return result;
}

static CheckBuiltinVectorResult check_builtin_vector_test_action(TypeTable* table, CBuiltinId id, const CheckValue* arguments, uint32_t count)
{
    const CheckBuiltinVectorAction action = { .id = id, .arguments = arguments, .argument_count = count };
    CheckBuiltinVectorResult result;
    check_builtin_vector_one(table, &action, &result);
    return result;
}

static bool check_builtin_vector_test_equal(const CheckBuiltinVectorResult* a, const CheckBuiltinVectorResult* b)
{
    bool equal = check_test_action_equal(&a->action, &b->action) && a->element_type == b->element_type && a->memory_type == b->memory_type &&
                 a->lane_count == b->lane_count && a->evaluated_argument_count == b->evaluated_argument_count && a->warnings == b->warnings &&
                 a->warning_operands == b->warning_operands && a->unsupported == b->unsupported && a->runtime_builtin == b->runtime_builtin &&
                 a->constraint == b->constraint && a->store_conversion == b->store_conversion && a->reads_memory == b->reads_memory &&
                 a->writes_memory == b->writes_memory && a->nondeterministic == b->nondeterministic &&
                 a->reads_fp_environment == b->reads_fp_environment && a->may_raise_fp_exceptions == b->may_raise_fp_exceptions &&
                 a->constant_evaluation_unmodeled == b->constant_evaluation_unmodeled && a->bool_splat_signed_true == b->bool_splat_signed_true;
    for (uint32_t i = 0; equal && i < 3; i += 1)
    {
        equal = check_test_value_equal(a->converted_arguments + i, b->converted_arguments + i) &&
                a->argument_conversions[i].type == b->argument_conversions[i].type && a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    return equal;
}

static void check_builtin_vector_test_names(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId integer = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId floating = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    const TypeId pointer = type_pointer(table, integer);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_vector_test_cases); i += 1)
    {
        const CheckBuiltinVectorTestCase fixture = check_builtin_vector_test_cases[i];
        const bool fp = fixture.id == CBUILTIN_ELEMENTWISE_FMA || fixture.id == CBUILTIN_ELEMENTWISE_SQRT;
        const TypeId operand = fp ? floating : integer;
        CheckValue arguments[3] = { check_builtin_vector_test_value(operand), check_builtin_vector_test_value(operand), check_builtin_vector_test_value(operand) };
        if (fixture.id == CBUILTIN_NONTEMPORAL_LOAD)
        {
            arguments[0].type = pointer;
        }
        if (fixture.id == CBUILTIN_NONTEMPORAL_STORE)
        {
            arguments[1].type = pointer;
        }
        const CheckBuiltinVectorAction action = { .id = fixture.id, .arguments = arguments, .argument_count = fixture.arity, .destination_type = floating };
        CheckBuiltinVectorResult result;
        check_builtin_vector_one(table, &action, &result);
        const TypeId expected = i >= 12 && i <= 17 ? BUSTER_TYPE_INT : fixture.id == CBUILTIN_CONVERTVECTOR ? floating :
            fixture.id == CBUILTIN_NONTEMPORAL_STORE ? BUSTER_TYPE_VOID : operand;
        check_builtin_vector_test_expect(test, cbuiltin_resolve(fixture.name, (uint32_t)strlen(fixture.name)) == fixture.id &&
            result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == expected && result.action.value.category == CHECK_RVALUE &&
            result.runtime_builtin == fixture.id && result.action.value.constant_kind == CHECK_CONSTANT_NONE &&
            (result.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED)) == 0 &&
            result.constant_evaluation_unmodeled == (i < 20), "every observed spelling retains exact result identity and an honest constant-payload gap");
        check_builtin_vector_test_expect(test, result.evaluated_argument_count == (fixture.id == CBUILTIN_NONDETERMINISTIC_VALUE ? 0 : fixture.arity) &&
            result.reads_memory == (fixture.id == CBUILTIN_NONTEMPORAL_LOAD) && result.writes_memory == (fixture.id == CBUILTIN_NONTEMPORAL_STORE) &&
            result.nondeterministic == (fixture.id == CBUILTIN_NONDETERMINISTIC_VALUE), "name-specific arity and evaluation/effect metadata are complete");
        CheckBuiltinVectorAction invalid = action;
        invalid.arguments = NULL;
        invalid.argument_count = fixture.id == CBUILTIN_SHUFFLEVECTOR ? 1 : fixture.id == CBUILTIN_ELEMENTWISE_CLZG ? 3 : fixture.arity + 1;
        check_builtin_vector_one(table, &invalid, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT && result.runtime_builtin == CBUILTIN_NONE,
            "invalid arity is rejected before reading any argument slice");
    }
}

// Clang 23's builtins past the 23 observed spellings, each against its independently stated type relations: the
// floating functions of one and two operands (copysign's operands compared whole), ldexp's exponents, the integer ones,
// the xor, floating maximum and minimum and floating sum reductions (a start value converted to the element), the
// masked ones' masks, pointers and passthru vectors, and the matrix ones, every call of which is an error without matrix
// types. Each family's floating environment effects are explicit: copysign's alone moves bits without them.
static void check_builtin_vector_test_added(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId vf = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    const TypeId ef = type_vector(table, BUSTER_TYPE_FLOAT, 4, true);
    const TypeId vd = type_vector(table, BUSTER_TYPE_DOUBLE, 4, false);
    const TypeId vi = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId vu = type_vector(table, BUSTER_TYPE_UINT, 4, false);
    const TypeId vs = type_vector(table, BUSTER_TYPE_SHORT, 8, false);
    const TypeId vl = type_vector(table, BUSTER_TYPE_LLONG, 4, false);
    const TypeId mask = type_vector(table, BUSTER_TYPE_BOOL, 4, true);
    const TypeId fp = type_pointer(table, BUSTER_TYPE_FLOAT);
    const TypeId cfp = type_pointer(table, type_qualify(table, BUSTER_TYPE_FLOAT, BUSTER_TYPE_CONST));
    const TypeId vfp = type_pointer(table, type_qualify(table, BUSTER_TYPE_FLOAT, BUSTER_TYPE_VOLATILE));
    const TypeId ip = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId efp = type_pointer(table, ef);
    static const CBuiltinId unary[] = {
        CBUILTIN_ELEMENTWISE_ACOS, CBUILTIN_ELEMENTWISE_ASIN, CBUILTIN_ELEMENTWISE_ATAN, CBUILTIN_ELEMENTWISE_COS, CBUILTIN_ELEMENTWISE_COSH,
        CBUILTIN_ELEMENTWISE_EXP, CBUILTIN_ELEMENTWISE_EXP10, CBUILTIN_ELEMENTWISE_EXP2, CBUILTIN_ELEMENTWISE_LOG, CBUILTIN_ELEMENTWISE_LOG10,
        CBUILTIN_ELEMENTWISE_LOG2, CBUILTIN_ELEMENTWISE_SIN, CBUILTIN_ELEMENTWISE_SINH, CBUILTIN_ELEMENTWISE_TAN, CBUILTIN_ELEMENTWISE_TANH,
        CBUILTIN_ELEMENTWISE_CEIL, CBUILTIN_ELEMENTWISE_FLOOR, CBUILTIN_ELEMENTWISE_TRUNC, CBUILTIN_ELEMENTWISE_RINT, CBUILTIN_ELEMENTWISE_NEARBYINT,
        CBUILTIN_ELEMENTWISE_ROUNDEVEN, CBUILTIN_ELEMENTWISE_ROUND, CBUILTIN_ELEMENTWISE_CANONICALIZE,
    };
    static const CBuiltinId binary[] = {
        CBUILTIN_ELEMENTWISE_ATAN2, CBUILTIN_ELEMENTWISE_FMOD, CBUILTIN_ELEMENTWISE_POW, CBUILTIN_ELEMENTWISE_COPYSIGN, CBUILTIN_ELEMENTWISE_MAXIMUM,
        CBUILTIN_ELEMENTWISE_MINIMUM, CBUILTIN_ELEMENTWISE_MAXNUM, CBUILTIN_ELEMENTWISE_MINNUM, CBUILTIN_ELEMENTWISE_MAXIMUMNUM,
        CBUILTIN_ELEMENTWISE_MINIMUMNUM,
    };
    CheckValue arguments[4] = { 0 };
    CheckBuiltinVectorResult result;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unary); i += 1)
    {
        arguments[0] = check_builtin_vector_test_value(vf);
        result = check_builtin_vector_test_action(table, unary[i], arguments, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == vf && result.reads_fp_environment &&
                                                   result.may_raise_fp_exceptions && result.runtime_builtin == unary[i] && result.constant_evaluation_unmodeled &&
                                                   check_graph_builtin_op(unary[i]) == CHECK_OP_BUILTIN_VECTOR,
                                         "a floating function of a vector of floats is of its type, in the floating environment");
        arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_LONG_DOUBLE);
        result = check_builtin_vector_test_action(table, unary[i], arguments, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_LONG_DOUBLE,
                                         "a floating function of a scalar is of its type");
        arguments[0] = check_builtin_vector_test_value(vi);
        result = check_builtin_vector_test_action(table, unary[i], arguments, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a floating function of integers is an error");
        arguments[1] = check_builtin_vector_test_value(vf);
        result = check_builtin_vector_test_action(table, unary[i], arguments, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a floating function of one operand takes one");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(binary); i += 1)
    {
        const bool copysign = binary[i] == CBUILTIN_ELEMENTWISE_COPYSIGN;
        arguments[0] = check_builtin_vector_test_value(vf);
        arguments[1] = check_builtin_vector_test_value(vf);
        result = check_builtin_vector_test_action(table, binary[i], arguments, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == vf &&
                                                   result.reads_fp_environment == !copysign && result.may_raise_fp_exceptions == !copysign,
                                         "a floating function of two operands is of their type; copysign alone moves bits without the environment");
        arguments[1] = check_builtin_vector_test_value(ef);
        result = check_builtin_vector_test_action(table, binary[i], arguments, 2);
        check_builtin_vector_test_expect(test, (result.action.diagnostic == CHECK_DIAG_NONE) == !copysign,
                                         "GNU and ext_vector_type operands of one element and lanes match but in copysign, which compares them whole");
        arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_FLOAT);
        result = check_builtin_vector_test_action(table, binary[i], arguments, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a vector and a scalar are of different types");
        arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_DOUBLE);
        result = check_builtin_vector_test_action(table, binary[i], arguments, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a double and a float are of different types");
        arguments[0] = check_builtin_vector_test_value(vi);
        arguments[1] = check_builtin_vector_test_value(vi);
        result = check_builtin_vector_test_action(table, binary[i], arguments, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a floating function of integers is an error");
    }
    // ldexp: a scalar exponent takes the integer promotions, a vector one none, the lanes the value's.
    arguments[0] = check_builtin_vector_test_value(vf);
    arguments[1] = check_builtin_vector_test_value(vi);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == vf && result.argument_conversions[1].type == vi &&
                                               result.reads_fp_environment,
                                     "ldexp of a vector by a vector of its lanes' exponents");
    arguments[1] = check_builtin_vector_test_value(vl);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "ldexp's exponents may be of any integer size");
    arguments[1] = check_builtin_vector_test_value(vs);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "ldexp's exponents have the value's lanes");
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_INT);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a vector's ldexp takes no scalar exponent");
    arguments[1] = check_builtin_vector_test_value(vf);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "ldexp's exponents are integers");
    arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_FLOAT);
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_SHORT);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_FLOAT &&
                                               result.argument_conversions[1].type == BUSTER_TYPE_INT &&
                                               (result.argument_conversions[1].flags & BUSTER_CHECK_CONVERT_PROMOTION) != 0,
                                     "a scalar exponent takes the integer promotions");
    arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_INT);
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_INT);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_LDEXP, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "ldexp's value is floating");
    // The integer functions: bitreverse, ctzg (with clzg's optional fallback), clmul, pdep and pext.
    static const CBuiltinId integer[] = { CBUILTIN_ELEMENTWISE_BITREVERSE, CBUILTIN_ELEMENTWISE_CTZG, CBUILTIN_ELEMENTWISE_CLMUL, CBUILTIN_ELEMENTWISE_PDEP,
                                          CBUILTIN_ELEMENTWISE_PEXT };
    static const uint32_t counts[] = { 1, 2, 2, 2, 2 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(integer); i += 1)
    {
        arguments[0] = check_builtin_vector_test_value(vu);
        arguments[1] = check_builtin_vector_test_value(type_vector(table, BUSTER_TYPE_UINT, 4, true));
        result = check_builtin_vector_test_action(table, integer[i], arguments, counts[i]);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == vu && !result.reads_fp_environment,
                                         "an integer function of vectors of one element and lanes is of the first's type");
        arguments[1] = check_builtin_vector_test_value(vi);
        result = check_builtin_vector_test_action(table, integer[i], arguments, counts[i]);
        check_builtin_vector_test_expect(test, (result.action.diagnostic == CHECK_DIAG_NONE) == (counts[i] == 1),
                                         "an integer function's second operand has the first's elements");
        arguments[0] = check_builtin_vector_test_value(vf);
        result = check_builtin_vector_test_action(table, integer[i], arguments, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "an integer function of floats is an error");
    }
    // The reductions: xor of integers, maximum and minimum of floating elements, the sums with their start values.
    arguments[0] = check_builtin_vector_test_value(vi);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_XOR, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_INT && result.lane_count == 4,
                                     "an xor reduction is of the element type");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_MAXIMUM, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a maximum reduction takes floating elements alone");
    arguments[0] = check_builtin_vector_test_value(vd);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_XOR, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INTEGER_REQUIRED, "an xor reduction takes integers alone");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_MINIMUM, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_DOUBLE && result.reads_fp_environment,
                                     "a minimum reduction is of the floating element type");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_ASSOC_FADD, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_DOUBLE,
                                     "a sum reduction's start value is optional");
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_FLOAT);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_ASSOC_FADD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.argument_conversions[1].type == BUSTER_TYPE_DOUBLE &&
                                               result.converted_arguments[1].type == BUSTER_TYPE_DOUBLE,
                                     "a sum reduction's start value converts to the element type");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_IN_ORDER_FADD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_DOUBLE,
                                     "an ordered sum reduction takes its start value");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_IN_ORDER_FADD, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "an ordered sum reduction requires its start value");
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_INT);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_ASSOC_FADD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a sum reduction's start value is floating");
    // The masked builtins.
    arguments[0] = check_builtin_vector_test_value(mask);
    arguments[1] = check_builtin_vector_test_value(fp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_LOAD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == ef && result.reads_memory &&
                                               result.memory_type == BUSTER_TYPE_FLOAT,
                                     "a masked load is the ext_vector_type of its pointer's type and its mask's lanes");
    arguments[1] = check_builtin_vector_test_value(cfp);
    arguments[2] = check_builtin_vector_test_value(ef);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_EXPAND_LOAD, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == ef,
                                     "a masked load reads through a const pointer, its passthru the result's type");
    arguments[2] = check_builtin_vector_test_value(vf);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_LOAD, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a masked load's passthru is the ext_vector_type itself");
    arguments[1] = check_builtin_vector_test_value(vfp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_LOAD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a masked load refuses a volatile pointer");
    arguments[1] = check_builtin_vector_test_value(efp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_LOAD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a masked load's pointer is to a scalar");
    arguments[0] = check_builtin_vector_test_value(vi);
    arguments[1] = check_builtin_vector_test_value(fp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_LOAD, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a masked builtin's mask is a vector of bools");
    arguments[0] = check_builtin_vector_test_value(mask);
    arguments[1] = check_builtin_vector_test_value(vf);
    arguments[2] = check_builtin_vector_test_value(fp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_STORE, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID && result.writes_memory,
                                     "a masked store of a vector of the pointer's type");
    arguments[2] = check_builtin_vector_test_value(ip);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_COMPRESS_STORE, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a masked store's vector has the pointer's type");
    arguments[1] = check_builtin_vector_test_value(vi);
    arguments[2] = check_builtin_vector_test_value(ef);
    arguments[3] = check_builtin_vector_test_value(fp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_SCATTER, arguments, 4);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.writes_memory, "a scatter by integer indices of the mask's lanes");
    arguments[1] = check_builtin_vector_test_value(vf);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_SCATTER, arguments, 4);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a scatter's indices are integers");
    arguments[1] = check_builtin_vector_test_value(vi);
    arguments[2] = check_builtin_vector_test_value(fp);
    result = check_builtin_vector_test_action(table, CBUILTIN_MASKED_GATHER, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == ef && result.reads_memory,
                                     "a gather is the ext_vector_type of its pointer's type");
    // The matrix builtins: an error whatever their operands, with their counts' own.
    static const CBuiltinId matrix[] = { CBUILTIN_MATRIX_TRANSPOSE, CBUILTIN_MATRIX_COLUMN_MAJOR_LOAD, CBUILTIN_MATRIX_COLUMN_MAJOR_STORE };
    static const uint32_t matrix_counts[] = { 1, 4, 3 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(matrix); i += 1)
    {
        for (uint32_t a = 0; a < 4; a += 1)
        {
            arguments[a] = check_builtin_vector_test_value(a == 0 ? vf : BUSTER_TYPE_INT);
        }
        result = check_builtin_vector_test_action(table, matrix[i], arguments, matrix_counts[i]);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "a matrix builtin's call is an error without matrix types");
        result = check_builtin_vector_test_action(table, matrix[i], arguments, matrix_counts[i] - 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a matrix builtin's count is its own");
    }
}

static void check_builtin_vector_test_math(CheckBuiltinVectorTest* test, TypeTable* table)
{
    CheckValue arguments[3] = { check_test_integer(BUSTER_TYPE_SCHAR, 2), check_test_integer(BUSTER_TYPE_SCHAR, 3), check_test_integer(BUSTER_TYPE_SCHAR, 1) };
    CheckBuiltinVectorResult result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ABS, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_SCHAR &&
        result.argument_conversions[0].type == BUSTER_TYPE_SCHAR && result.action.value.constant_kind == CHECK_CONSTANT_NONE && result.constant_evaluation_unmodeled,
        "signed char is not promoted and a scalar fold is not fabricated by the vector semantic layer");
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_SCHAR,
        "binary elementwise arithmetic performs no integer promotions");
    arguments[1].type = BUSTER_TYPE_INT;
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "binary elementwise arguments receive no usual arithmetic common type");
    arguments[0] = check_test_integer(BUSTER_TYPE_UCHAR, 2);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ABS, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "unsigned abs is rejected before any promotion");
    arguments[0] = check_test_integer(BUSTER_TYPE_BOOL, 1);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_POPCOUNT, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_BOOL,
        "integer-restricted elementwise operations retain scalar boolean identity");
    arguments[1] = arguments[0];
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_MIN, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "unrestricted min explicitly excludes scalar bool");

    const TypeId integer = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId extended = type_vector(table, BUSTER_TYPE_INT, 4, true);
    arguments[0] = check_builtin_vector_test_value(integer);
    arguments[1] = check_builtin_vector_test_value(extended);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == integer,
        "elementwise matching compares unqualified element identity and lanes across GNU/ext vector kinds");
    arguments[1].type = type_vector(table, BUSTER_TYPE_INT, 2, false);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "elementwise lane mismatches are rejected");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "elementwise builtins do not broadcast a scalar argument");
    arguments[1] = check_builtin_vector_test_value(integer);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_CLZG, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == integer,
        "elementwise clzg's optional fallback has the same vector type as its first operand");
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 4);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_CLZG, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "elementwise clzg does not use generic scalar-clzg fallback rules");

    const TypeId source_fp[] = { BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_BF16, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_FLOAT128 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(source_fp); i += 1)
    {
        arguments[0] = check_builtin_vector_test_value(source_fp[i]);
        CheckBuiltinVectorAction action = { .id = CBUILTIN_ELEMENTWISE_SQRT, .arguments = arguments, .argument_count = 1 };
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == (i == 0 ? BUSTER_TYPE_FLOAT : source_fp[i]),
            "source FP evaluation promotes only nonnative scalar __fp16 by default");
        action.fp_evaluation_type = BUSTER_TYPE_DOUBLE;
        check_builtin_vector_one(table, &action, &result);
        const TypeId expected = i < 4 ? BUSTER_TYPE_DOUBLE : source_fp[i];
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == expected,
            "explicit double evaluation widens scalars without narrowing long double or float128");
        action.fp_evaluation_type = BUSTER_TYPE_LONG_DOUBLE;
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == (i < 6 ? BUSTER_TYPE_LONG_DOUBLE : source_fp[i]),
            "explicit extended evaluation preserves wider source formats");
    }
    arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_HALF);
    CheckBuiltinVectorAction action = { .id = CBUILTIN_ELEMENTWISE_SQRT, .arguments = arguments, .argument_count = 1, .native_half = true };
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_HALF,
        "native half mode retains actual half type");
    arguments[0].type = type_vector(table, BUSTER_TYPE_FLOAT16, 4, true);
    action.fp_evaluation_type = BUSTER_TYPE_LONG_DOUBLE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == arguments[0].type,
        "FP evaluation options never promote vector elements");
    arguments[0] = check_builtin_vector_test_value(BUSTER_TYPE_FLOAT);
    arguments[1] = check_builtin_vector_test_value(BUSTER_TYPE_DOUBLE);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_MIN, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "floating min has no mixed float/double conversion");
    arguments[1] = arguments[0];
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_MIN, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.warnings & BUSTER_CHECK_BUILTIN_VECTOR_WARN_DEPRECATED) != 0,
        "floating min/max retain their Clang deprecation warning");
    const TypeId small_vector = type_vector(table, BUSTER_TYPE_SCHAR, 4, true);
    arguments[0] = check_builtin_vector_test_value(small_vector);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_ADD, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_SCHAR,
        "integer reduction result is its exact unpromoted element type");
    arguments[0].type = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_ADD, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "reduce_add does not silently accept a floating vector");
    result = check_builtin_vector_test_action(table, CBUILTIN_REDUCE_MAX, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_FLOAT,
        "reduce min/max accept floating elements without promoting the scalar result");

    arguments[0] = check_builtin_vector_test_value(integer);
    arguments[1] = arguments[0];
    arguments[0].flags = BUSTER_CHECK_SIDE_EFFECT;
    arguments[0].fault = CHECK_FAULT_SHIFT_COUNT;
    arguments[0].fault_node = 31;
    arguments[1].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    arguments[1].fault_node = 47;
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.evaluated_argument_count == 2 &&
        (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && result.action.value.fault == CHECK_FAULT_SHIFT_COUNT && result.action.value.fault_node == 31,
        "evaluated operands retain effects and the first evaluation fault");
    arguments[0] = check_builtin_vector_test_value(type_qualify(table, integer, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
    arguments[0].category = CHECK_LVALUE;
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ABS, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == integer &&
        (result.argument_conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0,
        "ordinary elementwise arguments perform lvalue conversion and remove local vector qualifiers");
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 814);
    const TypeId other = type_tag(table, BUSTER_TYPE_KIND_ENUM, 815);
    check_builtin_vector_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0) && type_enum_complete(table, other, BUSTER_TYPE_INT, 0),
        "independent complete enum fixtures");
    arguments[0] = check_builtin_vector_test_value(enumeration);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ABS, arguments, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == enumeration,
        "signed-underlying C enums follow Clang's signed integer predicate without promotion");
    arguments[1] = check_builtin_vector_test_value(other);
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ADD_SAT, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "matching underlying integer widths do not merge different enum types");
    arguments[1] = arguments[0];
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_MIN, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "scalar min/max's None gate specifically excludes enum types");
    uint32_t history[1024] = {0};
    history[table->types.index[type_index(enumeration)]] = 50;
    arguments[0].type = type_atomic(table, enumeration);
    arguments[0].category = CHECK_LVALUE;
    action = (CheckBuiltinVectorAction){ .id = CBUILTIN_ELEMENTWISE_POPCOUNT, .arguments = arguments, .argument_count = 1,
        .tag_completions = history, .tag_completion_count = BUSTER_ARRAY_LENGTH(history), .source_position = 49 };
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE,
        "lvalue conversion cannot expose an atomic enum's future completion");
    action.source_position = 50;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == enumeration,
        "the same converted enum becomes usable at its exact completion position");
}

static void check_builtin_vector_test_shuffle(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
    CheckValue arguments[66] = { check_builtin_vector_test_value(vector), check_builtin_vector_test_value(vector) };
    for (uint32_t i = 2; i < 66; i += 1)
    {
        arguments[i] = check_test_integer(BUSTER_TYPE_INT, (i - 2) % 8);
    }
    CheckBuiltinVectorResult result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 66);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == type_vector(table, BUSTER_TYPE_INT, 64, false) &&
        result.lane_count == 64 && result.evaluated_argument_count == 2 && result.constant_evaluation_unmodeled,
        "the full shuffle index list determines result width without a fixed three-operand truncation");
    arguments[65] = check_test_integer(BUSTER_TYPE_INT, 8);
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 66);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 65 && result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_RANGE,
        "an invalid final index beyond ordinary operand slots is diagnosed at its exact position");
    const CheckValue indices[] = { check_test_integer(BUSTER_TYPE_INT, (uint32_t)-1), check_test_integer(BUSTER_TYPE_INT128, ~(unsigned __int128)0),
        check_test_integer(BUSTER_TYPE_INT, (uint32_t)-2), check_test_integer(BUSTER_TYPE_UINT, UINT32_MAX), check_test_integer(BUSTER_TYPE_UINT128, (unsigned __int128)1 << 100) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(indices); i += 1)
    {
        arguments[2] = indices[i];
        result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 3);
        check_builtin_vector_test_expect(test, (result.action.diagnostic == CHECK_DIAG_NONE) == (i < 2),
            "only signed all-ones is the undef shuffle sentinel, including signed128");
    }
    arguments[2] = check_test_integer(BUSTER_TYPE_INT, 0);
    arguments[2].flags &= ~BUSTER_CHECK_ICE;
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE,
        "known integer payload without ICE provenance cannot become a shuffle selector");
    arguments[2] = check_test_integer(BUSTER_TYPE_INT, 0);
    arguments[2].fault = CHECK_FAULT_UNSUPPORTED_CONSTANT;
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
        (result.unsupported & BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_INDEX) != 0, "an unrepresented ICE payload is an explicit range-validation gap");
    arguments[0].type = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == arguments[0].type,
        "two-argument shuffle accepts a same-lane integer mask with a different data element type");
    arguments[1].type = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "two-argument shuffle mask must have integer representation");
    arguments[0].type = vector;
    arguments[1].type = type_vector(table, BUSTER_TYPE_INT, 4, true);
    arguments[2] = check_test_integer(BUSTER_TYPE_INT, 0);
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 3);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "scalar-index shuffle requires the same canonical vector kind as well as lanes");
    arguments[0].type = vector;
    CheckBuiltinVectorAction convert = { .id = CBUILTIN_CONVERTVECTOR, .arguments = arguments, .argument_count = 1,
        .destination_type = type_vector(table, BUSTER_TYPE_DOUBLE, 4, true) };
    check_builtin_vector_one(table, &convert, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == convert.destination_type &&
        result.evaluated_argument_count == 1 && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
        "convertvector's separate type operand chooses exact result identity without a fake value argument");
    convert.destination_type = type_vector(table, BUSTER_TYPE_DOUBLE, 2, true);
    check_builtin_vector_one(table, &convert, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "convertvector requires equal source/destination lane counts");
    arguments[0].type = type_atomic(table, vector);
    arguments[0].category = CHECK_LVALUE;
    convert.destination_type = vector;
    check_builtin_vector_one(table, &convert, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE,
        "convertvector checks the raw source vector type before atomic lvalue conversion");
    arguments[1] = check_builtin_vector_test_value(vector);
    result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE,
        "shufflevector checks raw vector operands before atomic lvalue conversion");
}

static void check_builtin_vector_test_shuffle_ice(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
    CheckValue arguments[3] = { check_builtin_vector_test_value(vector), check_builtin_vector_test_value(vector), check_test_integer(BUSTER_TYPE_INT, 0) };
    uint8_t witness = CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT;
    CheckBuiltinVectorAction action = { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = arguments, .argument_count = 3,
        .shuffle_index_ice = &witness, .shuffle_index_ice_count = 1 };
    CheckBuiltinVectorResult result;
    arguments[2].flags &= ~BUSTER_CHECK_ICE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.lane_count == 1 && result.evaluated_argument_count == 2,
        "an explicit strict source-ICE witness is independent of the local ICE flag");
    witness = CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU;
    arguments[2].flags |= BUSTER_CHECK_ICE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE,
        "a GNU source-ICE witness requires explicit permission even when a local ICE bit is present");
    action.allow_gnu_index_ice = true;
    arguments[2].flags &= ~BUSTER_CHECK_ICE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.warnings == 0,
        "permitted GNU source ICE carries no duplicate source-owned warning metadata");
    witness = CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE;
    arguments[2].flags |= BUSTER_CHECK_ICE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 2 &&
        result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE, "an explicit NONE witness overrides the local ICE fallback");
    witness = UINT8_MAX;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.action.diagnostic_operand == 2,
        "unknown source-ICE witness bytes are rejected at the exact index");
    for (uint32_t proof = CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT; proof <= CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU; proof += 1)
    {
        witness = (uint8_t)proof;
        for (uint32_t invalid = 0; invalid < 5; invalid += 1)
        {
            arguments[2] = check_test_integer(BUSTER_TYPE_INT, invalid == 3 ? 8 : 0);
            if (invalid == 0)
            {
                arguments[2].type = BUSTER_TYPE_DOUBLE;
            }
            else if (invalid == 1)
            {
                arguments[2].flags |= BUSTER_CHECK_SIDE_EFFECT;
            }
            else if (invalid == 2)
            {
                arguments[2].fault = CHECK_FAULT_SHIFT_COUNT;
            }
            else if (invalid == 4)
            {
                arguments[2].type = 0;
            }
            check_builtin_vector_one(table, &action, &result);
            check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == 2 &&
                (invalid != 3 || result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_RANGE),
                "source-ICE witnesses never bypass integer type, effects, faults, range or validity checks");
        }
        arguments[2] = check_test_integer(BUSTER_TYPE_INT, 0);
        arguments[2].constant_kind = CHECK_CONSTANT_NONE;
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
            (result.unsupported & BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_INDEX) != 0,
            "source ICE without an integer payload retains the explicit range-validation gap");
        arguments[2].constant_kind = CHECK_CONSTANT_INTEGER;
        arguments[2].fault = CHECK_FAULT_UNSUPPORTED_CONSTANT;
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE &&
            (result.unsupported & BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_INDEX) != 0,
            "source ICE with an unsupported payload fault retains the explicit range-validation gap");
    }
    arguments[2] = check_test_integer(BUSTER_TYPE_INT, 0);
    const uint32_t wrong_counts[] = { 0, 2, UINT32_MAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(wrong_counts); i += 1)
    {
        action.shuffle_index_ice_count = wrong_counts[i];
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT,
            "a supplied source-ICE slice must cover exactly the scalar shuffle indices");
    }
    action.shuffle_index_ice = NULL;
    action.shuffle_index_ice_count = 1;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a nonempty source-ICE slice requires storage");
    action.shuffle_index_ice_count = 0;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "NULL/zero keeps the existing strict local-ICE fallback");
    action.shuffle_index_ice = &witness;
    action.argument_count = 2;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.lane_count == 4,
        "the vector-mask shuffle form never reads an empty witness slice");
}

static void check_builtin_vector_test_memory(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId integer = type_vector(table, BUSTER_TYPE_INT, 4, false);
    const TypeId floating = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    const TypeId extended = type_vector(table, BUSTER_TYPE_INT, 4, true);
    CheckValue values[2] = { check_test_integer(BUSTER_TYPE_INT, 7),
        check_builtin_vector_test_value(type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE))) };
    CheckBuiltinVectorResult result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_STORE, values, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_VOID &&
        result.memory_type == BUSTER_TYPE_INT && result.writes_memory && result.evaluated_argument_count == 2,
        "nontemporal store strips pointed const/volatile and accepts Clang's unqualified destination initialization");
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_LOAD, values + 1, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_INT && result.reads_memory &&
        result.action.value.constant_kind == CHECK_CONSTANT_NONE, "nontemporal load returns the unqualified pointee without a fabricated value");
    values[0] = check_builtin_vector_test_value(floating);
    values[1].type = type_pointer(table, integer);
    CheckBuiltinVectorAction action = { .id = CBUILTIN_NONTEMPORAL_STORE, .arguments = values, .argument_count = 2 };
    for (uint32_t policy = CHECK_BUILTIN_VECTOR_LAX_DEFAULT; policy <= CHECK_BUILTIN_VECTOR_LAX_ALL; policy += 1)
    {
        action.lax_vector_conversions = (CheckBuiltinVectorLaxConversions)policy;
        check_builtin_vector_one(table, &action, &result);
        check_builtin_vector_test_expect(test, (result.action.diagnostic == CHECK_DIAG_NONE) ==
            (policy == CHECK_BUILTIN_VECTOR_LAX_DEFAULT || policy == CHECK_BUILTIN_VECTOR_LAX_ALL),
            "GNU17 lax default/all permits equal-width FP/integer vector bitcasts while none/integer rejects them");
    }
    values[0].type = type_vector(table, BUSTER_TYPE_SHORT, 8, false);
    action.lax_vector_conversions = CHECK_BUILTIN_VECTOR_LAX_INTEGER;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_conversion == CHECK_BUILTIN_VECTOR_STORE_BITCAST,
        "integer lax policy permits same-width vectors with different element counts");
    values[0] = check_test_integer(BUSTER_TYPE_BOOL, 1);
    values[1].type = type_pointer(table, extended);
    action.lax_vector_conversions = CHECK_BUILTIN_VECTOR_LAX_NONE;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_conversion == CHECK_BUILTIN_VECTOR_STORE_SPLAT &&
        result.bool_splat_signed_true && result.converted_arguments[0].constant_kind == CHECK_CONSTANT_NONE,
        "scalar bool splat carries signed-true metadata without inventing a vector payload");
    values[0] = check_builtin_vector_test_value(extended);
    values[1].type = type_pointer(table, type_vector(table, BUSTER_TYPE_BOOL, 4, true));
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.store_conversion == CHECK_BUILTIN_VECTOR_STORE_BOOLEAN_VECTOR,
        "ordinary ext-vector integer-to-bool initialization has explicit conversion metadata");
    const TypeId negative[] = { BUSTER_TYPE_VOID, type_atomic(table, BUSTER_TYPE_INT), type_array(table, BUSTER_TYPE_INT, 2),
        type_tag(table, BUSTER_TYPE_KIND_STRUCT, 812) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(negative); i += 1)
    {
        values[0] = check_builtin_vector_test_value(type_pointer(table, negative[i]));
        result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_LOAD, values, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "nontemporal excludes void, atomic, array and record pointees");
    }
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 813);
    check_builtin_vector_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0), "complete enum fixture construction");
    values[0] = check_builtin_vector_test_value(type_pointer(table, enumeration));
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_LOAD, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == enumeration,
        "Clang isIntegerType admits complete C enum nontemporal pointees");
    values[0].type = type_pointer(table, BUSTER_TYPE_COMPLEX_DOUBLE);
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_LOAD, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == BUSTER_TYPE_COMPLEX_DOUBLE &&
        (result.unsupported & BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_MEMORY_PAYLOAD) != 0,
        "Clang isFloatingType admits floating-complex nontemporal pointees without a host payload approximation");
    values[0] = check_builtin_vector_test_value(type_pointer(table, type_pointer(table, BUSTER_TYPE_INT)));
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_LOAD, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == type_pointer(table, BUSTER_TYPE_INT),
        "pointer-valued nontemporal objects retain their pointed type");
    values[0] = check_builtin_vector_test_value(type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST)));
    values[1] = check_builtin_vector_test_value(type_pointer(table, type_pointer(table, BUSTER_TYPE_INT)));
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_STORE, values, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && (result.warnings & BUSTER_CHECK_BUILTIN_VECTOR_WARN_QUALIFIERS) != 0 &&
        (result.warning_operands & 1) != 0, "ordinary pointer-valued store initialization retains qualifier-discard warnings");
    values[0] = check_builtin_vector_test_value(BUSTER_TYPE_COMPLEX_DOUBLE);
    values[1].type = type_pointer(table, BUSTER_TYPE_COMPLEX_DOUBLE);
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_STORE, values, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.memory_type == BUSTER_TYPE_COMPLEX_DOUBLE &&
        result.writes_memory && (result.unsupported & BUSTER_CHECK_BUILTIN_VECTOR_UNSUPPORTED_MEMORY_PAYLOAD) != 0,
        "matching complex nontemporal store remains valid with an explicit payload representation gap");
    const TypeId complex_vector_destinations[] = { integer, floating, extended, type_vector(table, BUSTER_TYPE_FLOAT, 4, true),
        type_vector(table, BUSTER_TYPE_BOOL, 4, true) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(complex_vector_destinations); i += 1)
    {
        values[1].type = type_pointer(table, complex_vector_destinations[i]);
        result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_STORE, values, 2);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == 0,
            "Clang's implicit-conversion diagnostic rejects complex scalars stored into integer, floating and bool vectors");
    }
    values[0] = check_builtin_vector_test_value(type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE));
    values[0].category = CHECK_LVALUE;
    values[0].flags = BUSTER_CHECK_SIDE_EFFECT;
    values[0].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
    values[0].fault_node = 91;
    result = check_builtin_vector_test_action(table, CBUILTIN_NONDETERMINISTIC_VALUE, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == values[0].type &&
        result.nondeterministic && result.evaluated_argument_count == 0 && result.action.value.fault == CHECK_FAULT_NONE &&
        result.argument_conversions[0].flags == 0 && result.action.value.constant_kind == CHECK_CONSTANT_NONE,
        "nondeterministic_value uses the raw qualified operand type and suppresses operand conversions/effects/faults");
    const TypeId nondeterministic_negative[] = { BUSTER_TYPE_COMPLEX_DOUBLE, type_bit_int(table, 17, false), enumeration, type_pointer(table, BUSTER_TYPE_INT),
        type_atomic(table, BUSTER_TYPE_INT) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(nondeterministic_negative); i += 1)
    {
        values[0] = check_builtin_vector_test_value(nondeterministic_negative[i]);
        result = check_builtin_vector_test_action(table, CBUILTIN_NONDETERMINISTIC_VALUE, values, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE, "nondeterministic_value requires actual BuiltinType or VectorType");
    }
    const TypeId nondeterministic_fp[] = { BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_BF16, BUSTER_TYPE_FLOAT128 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(nondeterministic_fp); i += 1)
    {
        values[0] = check_builtin_vector_test_value(nondeterministic_fp[i]);
        result = check_builtin_vector_test_action(table, CBUILTIN_NONDETERMINISTIC_VALUE, values, 1);
        check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.action.value.type == nondeterministic_fp[i] &&
            result.evaluated_argument_count == 0 && !result.reads_fp_environment && !result.may_raise_fp_exceptions,
            "nondeterministic raw floating types undergo no unary FP promotion or conversion effect");
    }
    uint32_t history[1024] = {0};
    const uint32_t tag = table->types.index[type_index(enumeration)];
    history[tag] = 50;
    values[0] = check_builtin_vector_test_value(type_pointer(table, enumeration));
    action = (CheckBuiltinVectorAction){ .id = CBUILTIN_NONTEMPORAL_LOAD, .arguments = values, .argument_count = 1,
        .tag_completions = history, .tag_completion_count = BUSTER_ARRAY_LENGTH(history), .source_position = 49 };
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPLETE, "later enum completion cannot leak backward into a pointed memory type");
    action.source_position = 50;
    check_builtin_vector_one(table, &action, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "pointed enum is usable at its exact source completion position");
}

static void check_builtin_vector_test_fp_effects(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const TypeId floating = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
    CheckValue values[2] = { check_builtin_vector_test_value(floating), check_builtin_vector_test_value(floating) };
    CheckBuiltinVectorResult result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_ABS, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && !result.reads_fp_environment && !result.may_raise_fp_exceptions,
        "same-format abs preserves sign-only behavior without an invented FP exception effect");
    result = check_builtin_vector_test_action(table, CBUILTIN_ELEMENTWISE_SQRT, values, 1);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.reads_fp_environment && result.may_raise_fp_exceptions &&
        (result.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "FP vector arithmetic retains environment and possible exception obligations");
    CheckBuiltinVectorAction convert = { .id = CBUILTIN_CONVERTVECTOR, .arguments = values, .argument_count = 1,
        .destination_type = type_vector(table, BUSTER_TYPE_INT, 4, false) };
    check_builtin_vector_one(table, &convert, &result);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.reads_fp_environment && result.may_raise_fp_exceptions,
        "numeric convertvector is not treated as a side-effect-free bitcast");
    values[0] = check_test_integer(BUSTER_TYPE_INT, 16777217);
    values[1] = check_builtin_vector_test_value(type_pointer(table, BUSTER_TYPE_FLOAT));
    result = check_builtin_vector_test_action(table, CBUILTIN_NONTEMPORAL_STORE, values, 2);
    check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.reads_fp_environment && result.may_raise_fp_exceptions,
        "ordinary integer-to-float store conversion keeps its FP rounding/exception requirements");
}

static void check_builtin_vector_test_pages(CheckBuiltinVectorTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t arguments_region = page != 0 ? ((66 * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t witness_region = page != 0 ? ((64 + 16 + page - 1) / page) * page : 0;
    const size_t input_region = page != 0 ? ((33 * sizeof(CheckBuiltinVectorAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((33 * sizeof(CheckBuiltinVectorResult) + 16 + page - 1) / page) * page : 0;
    uint8_t* argument_mapping = page != 0 ? mmap(NULL, arguments_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* witness_mapping = page != 0 ? mmap(NULL, witness_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* input_mapping = page != 0 ? mmap(NULL, input_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* output_mapping = page != 0 ? mmap(NULL, output_region + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    const bool ready = argument_mapping != MAP_FAILED && witness_mapping != MAP_FAILED && input_mapping != MAP_FAILED && output_mapping != MAP_FAILED &&
        mprotect(witness_mapping + witness_region, page, PROT_NONE) == 0 &&
        mprotect(argument_mapping + arguments_region, page, PROT_NONE) == 0 && mprotect(input_mapping + input_region, page, PROT_NONE) == 0 &&
        mprotect(output_mapping + output_region, page, PROT_NONE) == 0;
    check_builtin_vector_test_expect(test, ready, "guarded arguments/action/result allocation");
    if (ready)
    {
        const TypeId vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
        const uint32_t counts[] = { 2, 3, 7, 8, 9, 17, 33, 65, 66 };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            const uint32_t count = counts[c];
            CheckValue* arguments = (CheckValue*)(argument_mapping + arguments_region) - count;
            arguments[0] = check_builtin_vector_test_value(vector);
            arguments[1] = arguments[0];
            for (uint32_t i = 2; i < count; i += 1)
            {
                arguments[i] = check_test_integer(BUSTER_TYPE_INT, (i - 2) % 8);
            }
            CheckBuiltinVectorResult result = check_builtin_vector_test_action(table, CBUILTIN_SHUFFLEVECTOR, arguments, count);
            check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.lane_count == (count == 2 ? 4 : count - 2),
                "arbitrary-length shuffle slices stop exactly before a protected page");
            uint8_t* witnesses = witness_mapping + witness_region - (count - 2);
            for (uint32_t i = 2; i < count; i += 1)
            {
                arguments[i].flags &= ~BUSTER_CHECK_ICE;
                witnesses[i - 2] = CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT;
            }
            CheckBuiltinVectorAction witnessed = { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = arguments, .argument_count = count,
                .shuffle_index_ice = witnesses, .shuffle_index_ice_count = count - 2 };
            check_builtin_vector_one(table, &witnessed, &result);
            check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE && result.lane_count == (count == 2 ? 4 : count - 2),
                "source-ICE witnesses end exactly at a protected page without local ICE flags or padding");
            if (count > 2)
            {
                witnesses[count - 3] = CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU;
                check_builtin_vector_one(table, &witnessed, &result);
                check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == count - 1 &&
                    result.constraint == CHECK_BUILTIN_VECTOR_CONSTRAINT_ICE, "the last guarded GNU witness still requires permission");
                witnessed.allow_gnu_index_ice = true;
                check_builtin_vector_one(table, &witnessed, &result);
                check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_NONE, "permitted GNU witnesses retain the complete guarded index slice");
                witnesses[count - 3] = CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE;
                check_builtin_vector_one(table, &witnessed, &result);
                check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.action.diagnostic_operand == count - 1,
                    "the final guarded NONE witness is consumed rather than silently truncated");
                witnesses[count - 3] = CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT;
                arguments[count - 1] = check_test_integer(BUSTER_TYPE_INT, 8);
                check_builtin_vector_one(table, &witnessed, &result);
                check_builtin_vector_test_expect(test, result.action.diagnostic != CHECK_DIAG_NONE && result.action.diagnostic_operand == count - 1,
                    "guarded last shuffle index is validated rather than silently truncated");
            }
        }
        const CheckValue* inaccessible = (const CheckValue*)(argument_mapping + arguments_region);
        const uint8_t* inaccessible_witness = witness_mapping + witness_region;
        const CheckBuiltinVectorAction malformed[] =
        {
            { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = inaccessible, .argument_count = 3, .shuffle_index_ice_count = 1 },
            { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = inaccessible, .argument_count = 3, .shuffle_index_ice = inaccessible_witness },
            { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = inaccessible, .argument_count = 3, .shuffle_index_ice = inaccessible_witness, .shuffle_index_ice_count = 2 },
            { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = inaccessible, .argument_count = UINT32_MAX, .shuffle_index_ice = inaccessible_witness, .shuffle_index_ice_count = UINT32_MAX },
            { .id = CBUILTIN_SHUFFLEVECTOR, .arguments = inaccessible, .argument_count = 0, .shuffle_index_ice = inaccessible_witness, .shuffle_index_ice_count = UINT32_MAX },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(malformed); i += 1)
        {
            CheckBuiltinVectorResult result;
            check_builtin_vector_one(table, malformed + i, &result);
            check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT,
                "malformed witness pointer/count/full-index bounds are rejected before protected arguments or witness bytes are read");
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_builtin_vector_test_cases); i += 1)
        {
            const CheckBuiltinVectorTestCase fixture = check_builtin_vector_test_cases[i];
            const CheckBuiltinVectorAction invalid = { .id = fixture.id, .arguments = inaccessible, .argument_count = 0 };
            CheckBuiltinVectorResult result;
            check_builtin_vector_one(table, &invalid, &result);
            check_builtin_vector_test_expect(test, result.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "too-few arguments never dereference a protected slice");
        }
        CheckValue values[3] = { check_builtin_vector_test_value(vector), check_builtin_vector_test_value(vector),
            check_builtin_vector_test_value(type_pointer(table, vector)) };
        CheckValue shuffle_arguments[6] = { check_builtin_vector_test_value(vector), check_builtin_vector_test_value(vector) };
        for (uint32_t i = 2; i < BUSTER_ARRAY_LENGTH(shuffle_arguments); i += 1)
        {
            shuffle_arguments[i] = check_test_integer(BUSTER_TYPE_INT, i - 2);
            shuffle_arguments[i].flags &= ~BUSTER_CHECK_ICE;
        }
        const uint8_t witnesses[3][4] =
        {
            { CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT },
            { CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT },
            { CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT, CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT },
        };
        const CBuiltinId ids[] = { CBUILTIN_ELEMENTWISE_ADD_SAT, CBUILTIN_SHUFFLEVECTOR, CBUILTIN_CONVERTVECTOR, CBUILTIN_REDUCE_ADD,
            CBUILTIN_NONTEMPORAL_LOAD, CBUILTIN_NONDETERMINISTIC_VALUE };
        check_builtin_vector(NULL, NULL, NULL, 0);
        for (uint32_t count = 0; count <= 33; count += 1)
        {
            CheckBuiltinVectorAction* actions = (CheckBuiltinVectorAction*)(input_mapping + input_region) - count;
            CheckBuiltinVectorResult* outputs = (CheckBuiltinVectorResult*)(output_mapping + output_region) - count;
            memset((uint8_t*)outputs - 16, 0xa5, 16);
            CheckBuiltinVectorResult expected[33];
            for (uint32_t i = 0; i < count; i += 1)
            {
                const CBuiltinId id = ids[i % BUSTER_ARRAY_LENGTH(ids)];
                actions[i] = (CheckBuiltinVectorAction){ .id = id, .arguments = id == CBUILTIN_NONTEMPORAL_LOAD ? values + 2 : values,
                    .argument_count = id == CBUILTIN_ELEMENTWISE_ADD_SAT || id == CBUILTIN_SHUFFLEVECTOR ? 2 : 1,
                    .destination_type = type_vector(table, BUSTER_TYPE_FLOAT, 4, true) };
                if (id == CBUILTIN_SHUFFLEVECTOR)
                {
                    actions[i].arguments = shuffle_arguments;
                    actions[i].argument_count = BUSTER_ARRAY_LENGTH(shuffle_arguments);
                    actions[i].shuffle_index_ice = witnesses[(i / BUSTER_ARRAY_LENGTH(ids)) % BUSTER_ARRAY_LENGTH(witnesses)];
                    actions[i].shuffle_index_ice_count = BUSTER_ARRAY_LENGTH(witnesses[0]);
                    actions[i].allow_gnu_index_ice = ((i / BUSTER_ARRAY_LENGTH(ids)) & 1) != 0;
                }
                check_builtin_vector_one(table, actions + i, expected + i);
            }
            check_builtin_vector(table, actions, outputs, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_vector_test_expect(test, check_builtin_vector_test_equal(outputs + i, expected + i), "guarded record batch preserves scalar semantic output");
            }
            bool prefix = true;
            for (uint32_t i = 0; i < 16; i += 1)
            {
                prefix = prefix && ((uint8_t*)outputs)[(int)i - 16] == 0xa5;
            }
            check_builtin_vector_test_expect(test, prefix, "no batch writes before its logical result slice");
            for (uint32_t partition = 1; partition <= 9; partition += 1)
            {
                for (uint32_t first = 0; first < count;)
                {
                    const uint32_t length = count - first < partition ? count - first : partition;
                    check_builtin_vector(table, actions + first, outputs + first, length);
                    first += length;
                }
                for (uint32_t i = 0; i < count; i += 1)
                {
                    check_builtin_vector_test_expect(test, check_builtin_vector_test_equal(outputs + i, expected + i), "legal partitions preserve every vector semantic/effect field");
                }
            }
        }
    }
    if (argument_mapping != MAP_FAILED)
    {
        munmap(argument_mapping, arguments_region + page);
    }
    if (witness_mapping != MAP_FAILED)
    {
        munmap(witness_mapping, witness_region + page);
    }
    if (input_mapping != MAP_FAILED)
    {
        munmap(input_mapping, input_region + page);
    }
    if (output_mapping != MAP_FAILED)
    {
        munmap(output_mapping, output_region + page);
    }
}

static bool check_builtin_vector_test_spawn(const char* program, char* const* arguments, bool* rejected)
{
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
        ready = posix_spawnp(&child, program, &actions, NULL, arguments, environ) == 0;
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

static bool check_builtin_vector_test_clang_process(char* const* arguments, const char* artifact, bool* rejected)
{
    const char* files[] = { artifact };
    OracleRun run = {
        .purpose = "checker vector Clang oracle",
        .arguments = arguments,
        .files = files,
        .file_count = artifact != 0,
        .dependencies = true,
        .timeout = 10,
    };
    const int status = oracle_test_run(&run, 0, 0, 0, 0);
    const bool ready = status == 0 || status == 1;
    *rejected = ready && status == 1;
    return ready;
}

static bool check_builtin_vector_test_clang_run(const char* clang, const char* path, const char* option, bool* rejected)
{
    char* arguments[] = { (char*)clang, "-std=gnu17", "-march=x86-64", "-fsyntax-only", "-fno-color-diagnostics", (char*)path, (char*)option, NULL };
    const bool ready = check_builtin_vector_test_clang_process(arguments, 0, rejected);
    return ready;
}

static void check_builtin_vector_test_clang(CheckBuiltinVectorTest* test, const char* clang)
{
    const char* preamble = "typedef int vi __attribute__((vector_size(16))); typedef float vf __attribute__((vector_size(16)));\n"
        "typedef short vs __attribute__((vector_size(16))); typedef int ei __attribute__((ext_vector_type(4)));\n"
        "typedef _Bool eb __attribute__((ext_vector_type(4))); typedef signed char ec __attribute__((ext_vector_type(4)));\n"
        "typedef float ef __attribute__((ext_vector_type(4)));\n";
    char directory[] = "/tmp/buster-check-builtin-vector-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    bool ready = created;
    char path[160];
    char executable[160];
    snprintf(path, sizeof(path), "%s/source.c", directory);
    snprintf(executable, sizeof(executable), "%s/operand-effects", directory);
    const char* version = "_Static_assert(__clang_major__>23 || (__clang_major__==23 && (__clang_minor__>1 || (__clang_minor__==1 && __clang_patchlevel__>=1))),\"Clang 23.1.1 or newer required\");\n";
    bool rejected = false;
    ready = ready && parse_test_write(path, version, strlen(version)) && check_builtin_vector_test_clang_run(clang, path, NULL, &rejected);
    unlink(path);
    check_builtin_vector_test_expect(test, ready && !rejected, "external vector oracle must be Clang 23.1.1 or newer in GNU17");
    ready = ready && !rejected;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_builtin_vector_test_cases); i += 1)
    {
        const CheckBuiltinVectorTestCase fixture = check_builtin_vector_test_cases[i];
        const char* type = fixture.id == CBUILTIN_ELEMENTWISE_FMA || fixture.id == CBUILTIN_ELEMENTWISE_SQRT ? "vf" : "vi";
        char expression[384];
        if (fixture.id == CBUILTIN_CONVERTVECTOR)
        {
            snprintf(expression, sizeof(expression), "%s((vi){0},vf)", fixture.name);
        }
        else if (fixture.id == CBUILTIN_NONTEMPORAL_LOAD)
        {
            snprintf(expression, sizeof(expression), "%s((vi*)0)", fixture.name);
        }
        else if (fixture.id == CBUILTIN_NONTEMPORAL_STORE)
        {
            snprintf(expression, sizeof(expression), "%s((vi){0},(vi*)0)", fixture.name);
        }
        else
        {
            snprintf(expression, sizeof(expression), "%s((%s){0}%s%s)", fixture.name, type,
                fixture.arity >= 2 ? ",(vi){0}" : "", fixture.arity == 3 ? ",(vi){0}" : "");
            if (fixture.id == CBUILTIN_ELEMENTWISE_FMA)
            {
                snprintf(expression, sizeof(expression), "%s((vf){0},(vf){0},(vf){0})", fixture.name);
            }
        }
        const char* expected = i >= 12 && i <= 17 ? "int" : fixture.id == CBUILTIN_NONTEMPORAL_STORE ? "void" :
            fixture.id == CBUILTIN_CONVERTVECTOR ? "vf" : type;
        char source[1536];
        char description[192];
        snprintf(source, sizeof(source), "%s_Static_assert(__builtin_types_compatible_p(__typeof__(%s),%s),\"exact builtin result type\");\n", preamble, expression, expected);
        snprintf(path, sizeof(path), "%s/type-%u.c", directory, i);
        ready = parse_test_write(path, source, strlen(source)) && check_builtin_vector_test_clang_run(clang, path, NULL, &rejected);
        unlink(path);
        snprintf(description, sizeof(description), "Clang accepts %s with its exact source result type", fixture.name);
        check_builtin_vector_test_expect(test, ready && !rejected, description);
        snprintf(source, sizeof(source), "%svoid f(void){ (void)%s(); }\n", preamble, fixture.name);
        snprintf(path, sizeof(path), "%s/arity-%u.c", directory, i);
        ready = ready && parse_test_write(path, source, strlen(source)) && check_builtin_vector_test_clang_run(clang, path, NULL, &rejected);
        unlink(path);
        snprintf(description, sizeof(description), "Clang rejects zero operands for %s", fixture.name);
        check_builtin_vector_test_expect(test, ready && rejected, description);
    }
    const struct
    {
        const char* source;
        const char* option;
        bool rejected;
    } cases[] =
    {
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_abs((signed char)-3)),signed char),\"no integer promotion\");", NULL, false },
        { "enum { A=__builtin_elementwise_abs(-3) }; _Static_assert(A==3,\"scalar ICE evaluator is distinct from missing vector payloads\");", NULL, false },
        { "void f(void){ (void)__builtin_elementwise_abs((unsigned char)1); }", NULL, true },
        { "void f(void){ (void)__builtin_elementwise_add_sat((signed char)1,1); }", NULL, true },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_popcount((_Bool)1)),_Bool),\"bool integer predicate\");", NULL, false },
        { "void f(void){ (void)__builtin_elementwise_min((_Bool)1,(_Bool)0); }", NULL, true },
        { "void f(vi a,ei b){ (void)__builtin_elementwise_add_sat(a,b); }", NULL, false },
        { "void f(vi a){ (void)__builtin_elementwise_add_sat(a,1); }", NULL, true },
        { "void f(vi a){ (void)__builtin_elementwise_clzg(a,(vi){4}); }", NULL, false },
        { "void f(vi a){ (void)__builtin_elementwise_clzg(a,4); }", NULL, true },
        { "void f(vi a){ (void)__builtin_elementwise_fma(a,a,a); }", NULL, true },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_sqrt((_Float16)1)),_Float16),\"source FP format\");", NULL, false },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_sqrt((__fp16)1)),float),\"half promotion\");", NULL, false },
        { "void f(float a,double b){ (void)__builtin_elementwise_min(a,b); }", NULL, true },
        { "#pragma clang diagnostic error \"-Wdeprecated-builtins\"\nvoid f(float a){ (void)__builtin_elementwise_min(a,a); }", NULL, true },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_reduce_add((ec){1})),signed char),\"exact reduction element\");", NULL, false },
        { "void f(vf a){ (void)__builtin_reduce_add(a); }", NULL, true },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_reduce_max((vf){0})),float),\"FP reduction result\");", NULL, false },
        { "void f(vf a,vi mask){ (void)__builtin_shufflevector(a,mask); }", NULL, false },
        { "void f(vf a,vf mask){ (void)__builtin_shufflevector(a,mask); }", NULL, true },
        { "void f(vi a,ei b){ (void)__builtin_shufflevector(a,b,0); }", NULL, true },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,(__int128)-1); }", NULL, false },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,-2); }", NULL, true },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,~0u); }", NULL, true },
        { "void f(vi a,int index){ (void)__builtin_shufflevector(a,a,index); }", NULL, true },
        { "void f(vi a){ (void)__builtin_convertvector(a,vf); }", NULL, false },
        { "void f(vi a){ (void)__builtin_convertvector(a,int); }", NULL, true },
        { "void f(vi a){ (void)__builtin_convertvector(a,vs); }", NULL, true },
        { "void f(_Atomic(vi)*a){ (void)__builtin_convertvector(*a,vf); }", NULL, true },
        { "void f(_Atomic(vi)*a,vi b){ (void)__builtin_shufflevector(*a,b); }", NULL, true },
        { "void f(const volatile int*p){ (void)__builtin_nontemporal_load(p); __builtin_nontemporal_store(1,p); }", NULL, false },
        { "#pragma clang diagnostic error \"-Wincompatible-pointer-types-discards-qualifiers\"\nvoid f(int**p,const int*x){ __builtin_nontemporal_store(x,p); }", NULL, true },
        { "enum E{A}; void f(enum E*p){ (void)__builtin_nontemporal_load(p); }", NULL, false },
        { "void f(double _Complex*p,double _Complex a){ (void)__builtin_nontemporal_load(p); __builtin_nontemporal_store(a,p); }", NULL, false },
        { "void f(_Atomic(int)*p){ (void)__builtin_nontemporal_load(p); }", NULL, true },
        { "void f(void*p){ (void)__builtin_nontemporal_load(p); }", NULL, true },
        { "void f(int(*p)[2]){ (void)__builtin_nontemporal_load(p); }", NULL, true },
        { "void f(vi*p,vf a){ __builtin_nontemporal_store(a,p); }", NULL, false },
        { "void f(vi*p,vf a){ __builtin_nontemporal_store(a,p); }", "-flax-vector-conversions=none", true },
        { "void f(vi*p,vf a){ __builtin_nontemporal_store(a,p); }", "-flax-vector-conversions=integer", true },
        { "void f(vi*p,vf a){ __builtin_nontemporal_store(a,p); }", "-flax-vector-conversions=all", false },
        { "void f(vi*p,vs a){ __builtin_nontemporal_store(a,p); }", "-flax-vector-conversions=integer", false },
        { "void f(ei*p){ __builtin_nontemporal_store((_Bool)1,p); }", "-flax-vector-conversions=none", false },
        { "void f(eb*p,ei a){ __builtin_nontemporal_store(a,p); }", "-flax-vector-conversions=none", false },
        { "void f(ei*p,double _Complex a){ __builtin_nontemporal_store(a,p); }", NULL, true },
        { "enum E{A}; void f(enum E a){ (void)__builtin_nondeterministic_value(a); }", NULL, true },
        { "void f(int*p){ (void)__builtin_nondeterministic_value(p); }", NULL, true },
        { "void f(_BitInt(17) a){ (void)__builtin_nondeterministic_value(a); }", NULL, true },
        { "void f(double _Complex a){ (void)__builtin_nondeterministic_value(a); }", NULL, true },
        { "enum E{A=-1}; void f(enum E a){ (void)__builtin_elementwise_abs(a); (void)__builtin_elementwise_popcount(a); }", NULL, false },
        { "enum E{A=-1}; void f(enum E a){ (void)__builtin_elementwise_min(a,a); }", NULL, true },
        { "void f(ef*p,double _Complex a){ __builtin_nontemporal_store(a,p); }", NULL, true },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,1 << -1); }", NULL, false },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,1 << -1); }", "-Werror=shift-count-negative", true },
        { "void f(vi a){ (void)__builtin_shufflevector(a,a,0); }", "-Werror=shift-count-negative", false },
        { "void f(vf a,long double l){ (void)__builtin_elementwise_sin(a); (void)__builtin_elementwise_pow(a,a); (void)__builtin_elementwise_round(l); }", NULL, false },
        { "void f(vf a,ef b){ (void)__builtin_elementwise_copysign(a,b); }", NULL, true },
        { "void f(vf a,ef b){ (void)__builtin_elementwise_maximumnum(a,b); }", NULL, false },
        { "void f(vi a){ (void)__builtin_elementwise_floor(a); }", NULL, true },
        { "void f(vf a,vs b){ (void)__builtin_elementwise_ldexp(a,b); }", NULL, true },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_ldexp(1.0f,(short)2)),float),\"ldexp\");", NULL, false },
        { "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_reduce_assoc_fadd((vf){0},1.0)),float),\"start\");", NULL, false },
        { "void f(vf a){ (void)__builtin_reduce_in_order_fadd(a); }", NULL, true },
        { "void f(vf a){ (void)__builtin_reduce_assoc_fadd(a,1); }", NULL, true },
        { "void f(vi a){ (void)__builtin_reduce_maximum(a); }", NULL, true },
        { "void f(vi a,ei b){ (void)__builtin_reduce_xor(a); (void)__builtin_elementwise_ctzg(a,b); (void)__builtin_elementwise_pdep(a,b); }", NULL, false },
        { "void f(vi a,vs b){ (void)__builtin_elementwise_pext(a,b); }", NULL, true },
        { "void f(eb m,const float*p,ef v){ ef r=__builtin_masked_load(m,p,v); (void)r; }", NULL, false },
        { "void f(eb m,volatile float*p){ (void)__builtin_masked_load(m,p); }", NULL, true },
        { "void f(eb m,int*p,ef v){ __builtin_masked_store(m,v,p); }", NULL, true },
        { "void f(vi m,float*p){ (void)__builtin_masked_load(m,p); }", NULL, true },
        { "void f(float x){ (void)__builtin_matrix_transpose(x); }", NULL, true },
        { "void f(float*p){ (void)__builtin_matrix_column_major_load(p,2,2,2); }", NULL, true },
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char source[2048];
        char description[160];
        snprintf(source, sizeof(source), "%s%s\n", preamble, cases[i].source);
        snprintf(path, sizeof(path), "%s/constraint-%u.c", directory, i);
        ready = parse_test_write(path, source, strlen(source)) && check_builtin_vector_test_clang_run(clang, path, cases[i].option, &rejected);
        unlink(path);
        snprintf(description, sizeof(description), "GNU17 Clang vector source fixture %u disagrees (expected %s)", i, cases[i].rejected ? "rejection" : "acceptance");
        check_builtin_vector_test_expect(test, ready && rejected == cases[i].rejected, description);
    }
    for (uint32_t invalid = 0; ready && invalid < 2; invalid += 1)
    {
        char source[2048];
        int used = snprintf(source, sizeof(source), "%s_Static_assert(sizeof(__builtin_shufflevector((vi){0},(vi){0}", preamble);
        bool bounded = used > 0 && (size_t)used < sizeof(source);
        for (uint32_t i = 0; bounded && i < 64; i += 1)
        {
            const int bytes = snprintf(source + used, sizeof(source) - (size_t)used, ",%u", invalid && i == 63 ? 8u : i % 8);
            bounded = bytes > 0 && (size_t)bytes < sizeof(source) - (size_t)used;
            used += bounded ? bytes : 0;
        }
        if (bounded)
        {
            const int bytes = snprintf(source + used, sizeof(source) - (size_t)used, "))==64*sizeof(int),\"all shuffle indices\");\n");
            bounded = bytes > 0 && (size_t)bytes < sizeof(source) - (size_t)used;
            used += bounded ? bytes : 0;
        }
        snprintf(path, sizeof(path), "%s/shuffle-%u.c", directory, invalid);
        ready = bounded && parse_test_write(path, source, (size_t)used) && check_builtin_vector_test_clang_run(clang, path, NULL, &rejected);
        unlink(path);
        check_builtin_vector_test_expect(test, ready && rejected == (invalid != 0), "Clang checks the full64-index shuffle list including its last index");
    }
    const char* effect_source = "int main(void){ volatile int x=7; (void)__builtin_nondeterministic_value(x++); return x!=7; }\n";
    snprintf(path, sizeof(path), "%s/operand-effects.c", directory);
    ready = ready && parse_test_write(path, effect_source, strlen(effect_source));
    char object[160];
    snprintf(object, sizeof(object), "%s/operand-effects.o", directory);
    char* compile[] = { (char*)clang, "-std=gnu17", "-O0", "-march=x86-64", "-c", path, "-o", object, NULL };
    char* link[] = { (char*)clang, "-march=x86-64", object, "-o", executable, NULL };
    ready = ready && check_builtin_vector_test_clang_process(compile, object, &rejected);
    ready = ready && !rejected && check_builtin_vector_test_spawn(clang, link, &rejected);
    check_builtin_vector_test_expect(test, ready && !rejected, "Clang builds the original nondeterministic operand-effect oracle");
    if (ready && !rejected)
    {
        char* execute[] = { executable, NULL };
        ready = check_builtin_vector_test_spawn(executable, execute, &rejected);
        check_builtin_vector_test_expect(test, ready && !rejected, "nondeterministic_value evaluates its volatile increment operand zero times");
    }
    check_builtin_vector_test_expect(test, ready, "independent Clang vector oracle could not run");
    if (created)
    {
        unlink(object);
        unlink(executable);
        unlink(path);
        rmdir(directory);
    }
}

static __attribute__((unused)) int run_check_builtin_vector_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinVectorTest test = {0};
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
    check_builtin_vector_test_expect(&test, options, "usage: test-check-builtins-vector [--clang PATH]");
    if (options)
    {
        CheckTestTypes types = {0};
        const bool initialized = check_test_types_open(&types);
        check_builtin_vector_test_expect(&test, initialized, "vector builtin type-table allocation");
        if (initialized)
        {
            check_builtin_vector_test_names(&test, &types.table);
            check_builtin_vector_test_math(&test, &types.table);
            check_builtin_vector_test_added(&test, &types.table);
            check_builtin_vector_test_shuffle(&test, &types.table);
            check_builtin_vector_test_shuffle_ice(&test, &types.table);
            check_builtin_vector_test_memory(&test, &types.table);
            check_builtin_vector_test_fp_effects(&test, &types.table);
            check_builtin_vector_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_vector_test_clang(&test, clang);
    }
    printf("test-check-builtins-vector: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    oracle_cache_report(oracle_cache(), "test-check-builtins-vector");
    oracle_epoch_end(epoch);
    return result;
}
