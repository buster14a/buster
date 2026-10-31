// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's original RMW checker consumers. Direct expected conversions, effects and source-history constraints are
// independent of the production dispatch table. Guarded borrowed slices and legal batch partitions cover exact
// bounds. Original Clang source fixtures qualify constraints/warnings, and a small external native program checks
// ignored legacy tails and evaluated GNU weak/order expressions. No leaf emitter or helper harness is duplicated.
// Primary behavioral facts: llvmorg-23.1.1 SemaChecking.cpp:4924-5560,5589-5961 and CGAtomic.cpp:385-481,941-1059.
// All actual compilation/execution remains with the root's approved locked host invocation of this test command.

#include <poll.h>
#include <signal.h>
#include <time.h>

#define BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH 31u
#define BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS 8u

typedef struct CheckBuiltinAtomicRmwTest CheckBuiltinAtomicRmwTest;
struct CheckBuiltinAtomicRmwTest
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CheckBuiltinAtomicRmwTestCase CheckBuiltinAtomicRmwTestCase;
struct CheckBuiltinAtomicRmwTestCase
{
    CBuiltinId id;
    CheckBuiltinAtomicRmwOperation operation;
    CheckBuiltinAtomicRmwReturn returned;
    uint32_t fixed;
    bool legacy;
};

static const CheckBuiltinAtomicRmwTestCase check_builtin_atomic_rmw_test_cases[] =
{
    { CBUILTIN_ATOMIC_EXCHANGE_N, CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN, 6, false },
    { CBUILTIN_ATOMIC_FETCH_ADD, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_SUB, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_AND, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_XOR, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_OR, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_NAND, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_ADD_FETCH, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_SUB_FETCH, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_XOR_FETCH, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_OR_FETCH, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_NAND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_SYNC_FETCH_AND_ADD, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_SUB, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_OR, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_AND, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_XOR, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_FETCH_AND_NAND, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_ADD_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_ADD, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_SUB_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_SUB, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_OR_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_OR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_AND_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_AND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_XOR_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_XOR, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_NAND_AND_FETCH, CHECK_BUILTIN_ATOMIC_RMW_NAND, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 2, true },
    { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN, 3, true },
    { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, true },
    { CBUILTIN_SYNC_LOCK_TEST_AND_SET, CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 2, true },
    { CBUILTIN_SYNC_LOCK_RELEASE, CHECK_BUILTIN_ATOMIC_RMW_RELEASE, CHECK_BUILTIN_ATOMIC_RMW_VOID, 1, true },
    { CBUILTIN_ATOMIC_FETCH_MIN, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_FETCH_MAX, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_OLD_BITS, 3, false },
    { CBUILTIN_ATOMIC_MIN_FETCH, CHECK_BUILTIN_ATOMIC_RMW_MIN, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
    { CBUILTIN_ATOMIC_MAX_FETCH, CHECK_BUILTIN_ATOMIC_RMW_MAX, CHECK_BUILTIN_ATOMIC_RMW_NEW_BITS, 3, false },
};

#define BUSTER_CHECK_ATOMIC_RMW_TEST_CASES ((uint32_t)BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_test_cases))

static bool check_builtin_atomic_rmw_test_expect(CheckBuiltinAtomicRmwTest* test, bool condition, const char* description, uint32_t item)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-atomic-rmw: %s item%u\n", description, item);
        }
        test->failures += 1;
    }
    return condition;
}

static bool check_builtin_atomic_rmw_test_equal(const CheckBuiltinAtomicRmwResult* a, const CheckBuiltinAtomicRmwResult* b)
{
    bool equal = check_builtin_atomic_test_equal(&a->atomic, &b->atomic) && check_test_value_equal(&a->expected_address, &b->expected_address) &&
        check_test_value_equal(&a->expected_value, &b->expected_value) && a->operand_type == b->operand_type && a->fixed_argument_count == b->fixed_argument_count &&
        a->ignored_argument_count == b->ignored_argument_count && a->operation == b->operation && a->return_kind == b->return_kind && a->format == b->format && a->legacy == b->legacy &&
        a->object_written_on_success == b->object_written_on_success && a->expected_written_on_failure == b->expected_written_on_failure &&
        a->pointer_delta_unscaled == b->pointer_delta_unscaled && a->reads_fp_environment == b->reads_fp_environment && a->may_raise_fp_exceptions == b->may_raise_fp_exceptions;
    for (uint32_t i = 0; i < 6; i += 1)
    {
        equal &= check_test_value_equal(a->converted_arguments + i, b->converted_arguments + i);
    }
    return equal;
}

static CheckBuiltinAtomicRmwAction check_builtin_atomic_rmw_test_input(TypeTable* table, uint32_t item, TypeId value_type, CheckValue arguments[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS])
{
    const CheckBuiltinAtomicRmwTestCase fixture = check_builtin_atomic_rmw_test_cases[item];
    for (uint32_t i = 0; i < BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS; i += 1)
    {
        arguments[i] = check_test_integer(BUSTER_TYPE_INT, 4);
    }
    const TypeId object_type = type_qualify(table, value_type, BUSTER_TYPE_VOLATILE);
    arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, object_type), 21, 0);
    const bool pointer = type_kind(table, type_strip_aligned(table, value_type)) == BUSTER_TYPE_KIND_POINTER;
    CheckValue value = pointer ? check_builtin_atomic_test_pointer(value_type, 31, 4) : check_test_integer(BUSTER_TYPE_INT, 19);
    if (value_type == BUSTER_TYPE_FLOAT || value_type == BUSTER_TYPE_DOUBLE || value_type == BUSTER_TYPE_LONG_DOUBLE)
    {
        value = check_test_float(value_type, 3.75L);
    }
    arguments[1] = value;
    arguments[2] = value;
    uint8_t ice = 0;
    if (!fixture.legacy && fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE)
    {
        arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, value_type), 22, 0);
        arguments[3] = check_test_integer(BUSTER_TYPE_INT, 0);
        arguments[4] = check_test_integer(BUSTER_TYPE_INT, 5);
        arguments[5] = check_test_integer(BUSTER_TYPE_INT, 2);
        ice = (UINT32_C(1) << 4) | (UINT32_C(1) << 5);
    }
    else if (!fixture.legacy)
    {
        if (pointer && (fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_ADD || fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_SUB))
        {
            arguments[1] = check_test_integer(BUSTER_TYPE_INT, 4);
        }
        arguments[2] = check_test_integer(BUSTER_TYPE_INT, 5);
        ice = UINT32_C(1) << 2;
    }
    if (fixture.legacy)
    {
        arguments[fixture.fixed] = check_test_integer(BUSTER_TYPE_INT, 1);
        arguments[fixture.fixed].flags = BUSTER_CHECK_SIDE_EFFECT;
        arguments[fixture.fixed + 1] = check_test_integer(BUSTER_TYPE_INT, 0);
        arguments[fixture.fixed + 1].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
        arguments[fixture.fixed + 1].fault_node = 99;
    }
    const CheckBuiltinAtomicRmwAction result =
    {
        .source =
        {
            .id = fixture.id,
            .arguments = arguments,
            .argument_count = fixture.fixed + (fixture.legacy ? 2 : 0),
            .node = 148,
            .source_position = UINT32_MAX,
            .minimum_alignment = 16,
            .alignment_known = true,
        },
        .order_ice_arguments = ice,
    };
    return result;
}

static void check_builtin_atomic_rmw_test_values(CheckBuiltinAtomicRmwTest* test, TypeTable* table)
{
    const TypeId types[] = { BUSTER_TYPE_BOOL, BUSTER_TYPE_CHAR, BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT,
        BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_LLONG, BUSTER_TYPE_ULLONG, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, type_pointer(table, BUSTER_TYPE_INT) };
    const uint64_t sizes[] = { 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 8, 8, 4, 8, 8 };
    check_builtin_atomic_rmw_test_expect(test, BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_test_cases) == 34, "all 34 identities have independent expected semantic fixtures", 0);
    for (uint32_t item = 0; item < BUSTER_ARRAY_LENGTH(check_builtin_atomic_rmw_test_cases); item += 1)
    {
        const CheckBuiltinAtomicRmwTestCase fixture = check_builtin_atomic_rmw_test_cases[item];
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(types); t += 1)
        {
            CheckValue arguments[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
            const CheckBuiltinAtomicRmwAction input = check_builtin_atomic_rmw_test_input(table, item, types[t], arguments);
            CheckBuiltinAtomicRmwResult result;
            check_builtin_atomic_rmw_one(table, &input, &result);
            const bool arithmetic = fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_ADD || fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_SUB;
            const bool exchange = fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE;
            const bool compare = fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_COMPARE_EXCHANGE;
            const bool release = fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_RELEASE;
            // Clang's min and max take integers and floating values, never pointers.
            const bool ordered = fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_MIN || fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_MAX;
            const bool pointer = t == BUSTER_ARRAY_LENGTH(types) - 1;
            const bool floating = types[t] == BUSTER_TYPE_FLOAT || types[t] == BUSTER_TYPE_DOUBLE;
            const bool eligible = floating ? !fixture.legacy && (exchange || arithmetic || ordered) : !pointer || fixture.legacy || exchange || compare || arithmetic;
            if (!eligible)
            {
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.atomic.action.diagnostic_operand == 0,
                    "ineligible FP/pointer object is a source constraint", item * 16 + t);
            }
            else
            {
                const TypeId returned = fixture.returned == CHECK_BUILTIN_ATOMIC_RMW_BOOLEAN ? BUSTER_TYPE_BOOL : release ? BUSTER_TYPE_VOID : types[t];
                const uint32_t gap = floating && (arithmetic || ordered) ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW : types[t] == BUSTER_TYPE_BOOL &&
                    (arithmetic || fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_NAND) ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_BOOL_RMW : 0;
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.unsupported == gap && result.atomic.action.value.type == returned &&
                    result.atomic.value_type == types[t] && result.atomic.object_size == sizes[t] && result.atomic.required_alignment == sizes[t] && result.atomic.volatile_access &&
                    result.operation == fixture.operation && result.return_kind == fixture.returned && result.legacy == fixture.legacy && result.fixed_argument_count == fixture.fixed &&
                    result.ignored_argument_count == (fixture.legacy ? 2 : 0) && result.atomic.evaluated_operands == (UINT32_C(1) << fixture.fixed) - 1 &&
                    result.atomic.runtime_builtin == fixture.id && result.atomic.compiler_barrier && result.atomic.reads_memory == !release && result.atomic.writes_memory && result.atomic.rmw == !release &&
                    result.object_written_on_success == compare && result.expected_written_on_failure == (compare && !fixture.legacy), "typed RMW storage/result/effect contract without fabricated memory result", item * 16 + t);
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.value.constant_kind == CHECK_CONSTANT_NONE && result.atomic.action.value.fault == CHECK_FAULT_NONE &&
                    (result.atomic.action.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && (result.atomic.action.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_FLOAT_LITERAL)) == 0,
                    "legacy ignored tail effects/faults never contaminate effectful nonconstant result", item * 16 + t);
                const CheckBuiltinAtomicRmwFormat format = types[t] == BUSTER_TYPE_BOOL ? CHECK_BUILTIN_ATOMIC_RMW_BOOL_BITS :
                    types[t] == BUSTER_TYPE_FLOAT ? CHECK_BUILTIN_ATOMIC_RMW_F32_BITS : types[t] == BUSTER_TYPE_DOUBLE ? CHECK_BUILTIN_ATOMIC_RMW_F64_BITS : CHECK_BUILTIN_ATOMIC_RMW_INTEGER_BITS;
                check_builtin_atomic_rmw_test_expect(test, result.format == format && result.pointer_delta_unscaled == (pointer && arithmetic) &&
                    result.operand_type == (pointer && arithmetic && !fixture.legacy ? BUSTER_TYPE_LONG : types[t]), "raw format and GNU ptrdiff versus legacy pointer-value operand", item * 16 + t);
                if (fixture.legacy)
                {
                    check_builtin_atomic_rmw_test_expect(test, (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST) != 0 &&
                        ((result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_NAND_CHANGED) != 0) == (fixture.operation == CHECK_BUILTIN_ATOMIC_RMW_NAND) &&
                        result.atomic.success_order == (release ? 3 : 5), "legacy warning classes and source Release versus sequentially-consistent order", item);
                }
                if (compare && !fixture.legacy)
                {
                    check_builtin_atomic_rmw_test_expect(test, result.expected_address.type == type_pointer(table, types[t]) && result.converted_arguments[3].type == BUSTER_TYPE_BOOL &&
                        result.converted_arguments[3].constant_kind == CHECK_CONSTANT_INTEGER && result.converted_arguments[3].integer == 0 && result.atomic.success_order == 5 && result.atomic.failure_order == 2,
                        "GNU CAS expected address, bool weak and independent order conversion", item);
                }
            }
        }
    }
}

static void check_builtin_atomic_rmw_test_constraints(CheckBuiltinAtomicRmwTest* test, TypeTable* table)
{
    CheckValue arguments[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
    CheckBuiltinAtomicRmwAction input = check_builtin_atomic_rmw_test_input(table, 1, BUSTER_TYPE_INT, arguments);
    CheckBuiltinAtomicRmwResult result;
    const uint32_t qualifiers[] = { BUSTER_TYPE_CONST, BUSTER_TYPE_VOLATILE, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(qualifiers); i += 1)
    {
        arguments[1].type = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, qualifiers[i]));
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.expected_address.type == type_pointer(table, BUSTER_TYPE_INT) &&
            (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_QUALIFIERS) != 0 && (result.atomic.warning_operands & 2) != 0 && result.expected_written_on_failure,
            "expected-pointer qualifier loss is an ordinary conversion warning", i);
    }
    arguments[1].type = type_pointer(table, BUSTER_TYPE_UINT);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN) != 0 &&
        (result.atomic.warning_operands & 2) != 0, "expected-pointer signedness uses ordinary pointer warning", 0);
    arguments[1].type = type_pointer(table, BUSTER_TYPE_DOUBLE);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.atomic.action.diagnostic_operand == 1, "incompatible expected pointer is a real constraint", 0);
    arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_INT), 0, 0);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_NULL) != 0 &&
        (result.atomic.warning_operands & 2) != 0, "null expected pointer retains warning rather than inventing a source access", 0);
    input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_INT, arguments);
    arguments[0].type = type_pointer(table, type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED && result.atomic.action.diagnostic_operand == 0,
        "const target object is a hard constraint unlike const expected pointer", 0);
    const uint32_t bit_counts[] = { 1, 2, 4, 8, 16, 24, 32, 64, 128, 256, 257 };
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(bit_counts); b += 1)
    {
        const uint32_t bits = bit_counts[b];
        const TypeId type = type_bit_int(table, bits, bits == 1);
        input = check_builtin_atomic_rmw_test_input(table, 2, type, arguments);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.atomic.action.diagnostic_operand == 0,
            "GNU rejects every BitInt object", bits);
        input = check_builtin_atomic_rmw_test_input(table, 14, type, arguments);
        check_builtin_atomic_rmw_one(table, &input, &result);
        const bool allowed = bits <= 128 && (bits & (bits - 1)) == 0;
        const uint32_t unsupported = bits < 8 ? BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER : bits == 128 ? BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_WIDTH : 0;
        check_builtin_atomic_rmw_test_expect(test, allowed ? result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.unsupported == unsupported :
            result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "legacy BitInt source eligibility is separate from subbyte/wide runtime support", bits);
    }
    input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_INT, arguments);
    arguments[1] = (CheckValue){ .type = type_bit_int(table, 256, false), .category = CHECK_RVALUE };
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0 &&
        result.atomic.store_value.constant_kind == CHECK_CONSTANT_NONE && result.atomic.action.value.fault == CHECK_FAULT_NONE, "wide desired conversion stays source-valid and explicitly unsupported without invented low bits/fault", 0);
    input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_LONG_DOUBLE, arguments);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "direct x87 long-double object is ineligible for GNU exchange", 0);
    input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_INT, arguments);
    input.source.alignment_known = false;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_UNKNOWN_ALIGNMENT) != 0,
        "unknown actual address alignment preserves valid source semantics", 0);
    input.source.alignment_known = true;
    input.source.minimum_alignment = 1;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ALIGNMENT) != 0 &&
        (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ALIGNMENT) != 0, "packed/reduced address alignment refuses native path explicitly", 0);
    input.source.minimum_alignment = 3;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "malformed alignment proof is invalid", 0);
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 148);
    const bool complete = enumeration != 0 && type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0);
    check_builtin_atomic_rmw_test_expect(test, complete, "source-history enum fixture", 0);
    if (complete)
    {
        uint32_t completions[512] = { 0 };
        const uint32_t tag = table->types.index[type_index(enumeration)];
        completions[tag] = 20;
        input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_INT, arguments);
        arguments[1] = check_test_integer(enumeration, UINT32_MAX);
        input.source.tag_completions = completions;
        input.source.tag_completion_count = BUSTER_ARRAY_LENGTH(completions);
        input.source.node = 999;
        input.source.source_position = 19;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPLETE && result.atomic.action.diagnostic_operand == 1,
            "future enum definition is invisible at source position regardless of action node", 0);
        input.source.source_position = 20;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.store_value.type == BUSTER_TYPE_INT && result.atomic.store_value.integer == UINT32_MAX,
            "visible source enum follows ordinary value conversion", 0);
    }
    const TypeId bool_enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 149);
    const bool bool_complete = bool_enumeration != 0 && type_enum_complete(table, bool_enumeration, BUSTER_TYPE_BOOL, 0);
    check_builtin_atomic_rmw_test_expect(test, bool_complete, "subbyte enum fixture with bool underlying type", 0);
    if (bool_complete)
    {
        input = check_builtin_atomic_rmw_test_input(table, 0, bool_enumeration, arguments);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE &&
            result.atomic.unsupported == BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_SUBBYTE_INTEGER,
            "enum with bool underlying type stays source-valid and refuses integer byte arithmetic", 0);
    }
}

static void check_builtin_atomic_rmw_test_orders(CheckBuiltinAtomicRmwTest* test, TypeTable* table)
{
    CheckValue arguments[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
    CheckBuiltinAtomicRmwAction input = check_builtin_atomic_rmw_test_input(table, 1, BUSTER_TYPE_INT, arguments);
    const uint32_t orders[] = { 0, 1, 2, 3, 4, 5, 6, UINT32_MAX };
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(orders); s += 1)
    {
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(orders); f += 1)
        {
            for (uint32_t witnessed = 0; witnessed < 4; witnessed += 1)
            {
                arguments[4] = check_test_integer(BUSTER_TYPE_INT, orders[s]);
                arguments[5] = check_test_integer(BUSTER_TYPE_INT, orders[f]);
                input.order_ice_arguments = ((witnessed & 1) != 0 ? 1u << 4 : 0) | ((witnessed & 2) != 0 ? 1u << 5 : 0);
                CheckBuiltinAtomicRmwResult result;
                check_builtin_atomic_rmw_one(table, &input, &result);
                const bool bad_success = orders[s] > 5;
                const bool bad_failure = orders[f] != 0 && orders[f] != 1 && orders[f] != 2 && orders[f] != 5;
                const uint32_t warning_operands = (bad_success && (witnessed & 1) != 0 ? 1u << 4 : 0) | (bad_failure && (witnessed & 2) != 0 ? 1u << 5 : 0);
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.success_order == orders[s] && result.atomic.failure_order == orders[f] &&
                    result.atomic.known_orders == (BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER | BUSTER_CHECK_BUILTIN_ATOMIC_RMW_KNOWN_FAILURE_ORDER) &&
                    ((result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0) == (warning_operands != 0) && result.atomic.warning_operands == warning_operands,
                    "order diagnostics require separate ICE witness, with no failure<=success condition", s * 32 + f * 4 + witnessed);
            }
        }
    }
    input = check_builtin_atomic_rmw_test_input(table, 1, BUSTER_TYPE_INT, arguments);
    input.order_ice_arguments = 0;
    for (uint32_t i = 3; i < 6; i += 1)
    {
        arguments[i] = (CheckValue){ .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_SIDE_EFFECT };
    }
    CheckBuiltinAtomicRmwResult result;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.evaluated_operands == 63 && result.atomic.known_orders == 0 &&
        result.converted_arguments[3].type == BUSTER_TYPE_BOOL && result.converted_arguments[4].type == BUSTER_TYPE_INT && result.converted_arguments[5].type == BUSTER_TYPE_INT && result.atomic.warnings == 0,
        "dynamic weak/orders retain once-evaluated operands without false ICE constraints", 0);
    arguments[4].fault = CHECK_FAULT_SHIFT_COUNT;
    arguments[4].fault_node = 701;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.action.value.fault == CHECK_FAULT_SHIFT_COUNT && result.atomic.action.value.fault_node == 701,
        "evaluated order potential fault survives without being promoted to source constraint", 0);
    input.order_ice_arguments = 1u << 4;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INVALID_TYPE && result.atomic.action.diagnostic_operand == 4,
        "contradictory ICE witness cannot certify faulting dynamic order", 0);
    input.order_ice_arguments = 1u << 3;
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "weak position cannot carry an order ICE proof", 0);
    input = check_builtin_atomic_rmw_test_input(table, 1, BUSTER_TYPE_INT, arguments);
    arguments[3] = check_test_integer(BUSTER_TYPE_INT, 2);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.converted_arguments[3].integer == 1,
        "weak numeric2 converts by C truthiness rather than low-bit truncation", 0);
    arguments[4] = (CheckValue){ .type = type_bit_int(table, 256, false), .category = CHECK_RVALUE };
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && (result.atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_CONVERSION) != 0 &&
        (result.atomic.known_orders & BUSTER_CHECK_BUILTIN_ATOMIC_KNOWN_SUCCESS_ORDER) == 0 && result.atomic.action.value.fault == CHECK_FAULT_NONE,
        "source ICE proof with unrepresented wide order remains valid unsupported conversion", 0);
    input = check_builtin_atomic_rmw_test_input(table, 0, BUSTER_TYPE_BOOL, arguments);
    arguments[1] = check_test_integer(BUSTER_TYPE_INT, 2);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.unsupported == 0 && result.atomic.store_value.type == BUSTER_TYPE_BOOL &&
        result.atomic.store_value.integer == 1, "canonical bool exchange desired value uses truth conversion", 0);
}

// A source action of `count` arguments for an identity outside the shared fixture table.
static CheckBuiltinAtomicRmwAction check_builtin_atomic_rmw_test_call(CBuiltinId id, const CheckValue* arguments, uint32_t count, uint8_t ice)
{
    const CheckBuiltinAtomicRmwAction result =
    {
        .source =
        {
            .id = id,
            .arguments = arguments,
            .argument_count = count,
            .node = 148,
            .source_position = UINT32_MAX,
            .minimum_alignment = 16,
            .alignment_known = true,
        },
        .order_ice_arguments = ice,
    };
    return result;
}

// #148's rules past the shared table: __sync_swap, clang's legacy minimum and maximum of their prototypes, the wrapping
// increment and decrement, and GNU's byte flag.
static void check_builtin_atomic_rmw_test_added(CheckBuiltinAtomicRmwTest* test, TypeTable* table)
{
    CheckValue arguments[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
    for (uint32_t i = 0; i < BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS; i += 1)
    {
        arguments[i] = check_test_integer(BUSTER_TYPE_INT, 5);
    }
    CheckBuiltinAtomicRmwResult result;
    // __sync_swap is __sync_lock_test_and_set's exchange, protected variables and all.
    arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_SHORT), 21, 0);
    CheckBuiltinAtomicRmwAction input = check_builtin_atomic_rmw_test_call(CBUILTIN_SYNC_SWAP, arguments, 3, 0);
    check_builtin_atomic_rmw_one(table, &input, &result);
    check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.operation == CHECK_BUILTIN_ATOMIC_RMW_EXCHANGE &&
        result.legacy && result.atomic.action.value.type == BUSTER_TYPE_SHORT && result.ignored_argument_count == 1 && result.atomic.success_order == 5 &&
        (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST) != 0, "__sync_swap is a legacy exchange of its object's type", 0);
    // The legacy minimum and maximum take their prototypes' int or unsigned int through volatile pointers to them, exactly
    // two arguments, and order signed and unsigned values as their names say.
    const CBuiltinId ordered[] = { CBUILTIN_SYNC_FETCH_AND_MIN, CBUILTIN_SYNC_FETCH_AND_MAX, CBUILTIN_SYNC_FETCH_AND_UMIN, CBUILTIN_SYNC_FETCH_AND_UMAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ordered); i += 1)
    {
        const TypeId own = i < 2 ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT;
        const TypeId other = i < 2 ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT;
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, own), 21, 0);
        arguments[1] = check_test_integer(BUSTER_TYPE_LONG, 7);
        input = check_builtin_atomic_rmw_test_call(ordered[i], arguments, 2, 0);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.action.value.type == own &&
            result.atomic.value_type == own && result.atomic.store_value.type == own && result.atomic.object_type == type_qualify(table, own, BUSTER_TYPE_VOLATILE) &&
            result.operation == (i % 2 == 0 ? CHECK_BUILTIN_ATOMIC_RMW_MIN : CHECK_BUILTIN_ATOMIC_RMW_MAX) && result.atomic.success_order == 5 &&
            (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_WARN_IMPLICIT_SEQ_CST) == 0 && result.atomic.evaluated_operands == 3,
            "legacy minimum and maximum of their prototype's value type", i);
        input.source.argument_count = 3;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "a prototype takes no protected variables", i);
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, other), 21, 0);
        input.source.argument_count = 2;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.value_type == own &&
            (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_POINTER_SIGN) != 0, "the other signedness's pointer converts with clang's pointer-sign warning", i);
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_LONG), 21, 0);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.atomic.action.diagnostic_operand == 0,
            "an incompatible pointer is clang's error", i);
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_VOID), 21, 0);
        input.source.minimum_alignment = 1;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.alignment_known && result.atomic.minimum_alignment == 4 &&
            result.atomic.unsupported == 0, "a void pointer's object has the prototype's alignment, as clang's code generation gives it", i);
    }
    // The wrapping increment and decrement take integers, bool among them, but no pointer or floating value.
    const CBuiltinId wrapping[] = { CBUILTIN_ATOMIC_FETCH_UINC, CBUILTIN_ATOMIC_FETCH_UDEC };
    const TypeId wrapped[] = { BUSTER_TYPE_BOOL, BUSTER_TYPE_SCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT, BUSTER_TYPE_ULONG, BUSTER_TYPE_FLOAT, type_pointer(table, BUSTER_TYPE_INT) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(wrapping); i += 1)
    {
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(wrapped); t += 1)
        {
            arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, wrapped[t]), 21, 0);
            arguments[1] = check_test_integer(BUSTER_TYPE_INT, 3);
            arguments[2] = check_test_integer(BUSTER_TYPE_INT, 7);
            input = check_builtin_atomic_rmw_test_call(wrapping[i], arguments, 3, 1u << 2);
            check_builtin_atomic_rmw_one(table, &input, &result);
            const bool integer = t < 5;
            check_builtin_atomic_rmw_test_expect(test, integer ? result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.action.value.type == wrapped[t] &&
                result.operation == (i == 0 ? CHECK_BUILTIN_ATOMIC_RMW_UINC : CHECK_BUILTIN_ATOMIC_RMW_UDEC) && result.atomic.unsupported == 0 && result.atomic.rmw &&
                (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0 :
                result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE, "wrapping increment and decrement of integers alone, an invalid order 7 warned", i * 8 + t);
        }
    }
    // GNU's byte flag: any pointer to an object that isn't const, its first byte a char's, the order its second argument.
    const TypeId flags[] = { BUSTER_TYPE_VOID, BUSTER_TYPE_CHAR, BUSTER_TYPE_LONG, type_tag(table, BUSTER_TYPE_KIND_STRUCT, 151), type_bit_int(table, 32, false) };
    const uint32_t orders[] = { 0, 1, 2, 3, 4, 5, 6 };
    for (uint32_t clear = 0; clear < 2; clear += 1)
    {
        const CBuiltinId id = clear != 0 ? CBUILTIN_ATOMIC_CLEAR : CBUILTIN_ATOMIC_TEST_AND_SET;
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(flags); t += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(orders); o += 1)
            {
                arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, flags[t], BUSTER_TYPE_VOLATILE)), 21, 0);
                arguments[1] = check_test_integer(BUSTER_TYPE_INT, orders[o]);
                input = check_builtin_atomic_rmw_test_call(id, arguments, 2, 1u << 1);
                input.source.alignment_known = false;
                check_builtin_atomic_rmw_one(table, &input, &result);
                const bool invalid = orders[o] > 5 || (clear != 0 && (orders[o] == 1 || orders[o] == 2 || orders[o] == 4));
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.value_type == BUSTER_TYPE_CHAR &&
                    result.atomic.object_type == type_qualify(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_VOLATILE) && result.atomic.volatile_access &&
                    result.atomic.object_size == 1 && result.atomic.alignment_known && result.atomic.unsupported == 0 &&
                    result.atomic.action.value.type == (clear != 0 ? BUSTER_TYPE_VOID : BUSTER_TYPE_BOOL) && result.atomic.rmw == (clear == 0) &&
                    result.atomic.reads_memory == (clear == 0) && result.atomic.evaluated_operands == 3 && result.atomic.success_order == orders[o] &&
                    ((result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0) == invalid,
                    "the byte flag of any non-const object, with its store's or any order", clear * 64 + t * 8 + o);
            }
        }
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, BUSTER_TYPE_VOID, BUSTER_TYPE_CONST)), 21, 0);
        arguments[1] = check_test_integer(BUSTER_TYPE_INT, 5);
        input = check_builtin_atomic_rmw_test_call(id, arguments, 2, 1u << 1);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "a const object's byte flag is clang's error", clear);
        input.order_ice_arguments = 1u << 2;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_INVALID_TYPE, "the byte flag's order proof is its second argument's", clear);
        arguments[0] = check_test_integer(BUSTER_TYPE_INT, 0);
        input = check_builtin_atomic_rmw_test_call(id, arguments, 2, 0);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_POINTER_REQUIRED, "the byte flag takes a pointer", clear);
    }
    // clang's IEEE maximum and minimum take its floating types alone, complex ones among them, but long double: a float's
    // and a double's with leaves (F32 and F64), the others' valid but refused at runtime; the operand converts to the
    // object's type, an invalid order is warned, and a const object is clang's error.
    const CBuiltinId ieee[] = { CBUILTIN_ATOMIC_FETCH_FMAXIMUM, CBUILTIN_ATOMIC_FETCH_FMAXIMUM_NUM, CBUILTIN_ATOMIC_FETCH_FMINIMUM, CBUILTIN_ATOMIC_FETCH_FMINIMUM_NUM };
    const CheckBuiltinAtomicRmwOperation ieee_operations[] = { CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM, CHECK_BUILTIN_ATOMIC_RMW_FMAXIMUM_NUM, CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM,
                                                               CHECK_BUILTIN_ATOMIC_RMW_FMINIMUM_NUM };
    const TypeId floating[] = { BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_BF16, BUSTER_TYPE_FLOAT128, type_complex(table, BUSTER_TYPE_FLOAT),
                                type_complex(table, BUSTER_TYPE_LONG_DOUBLE), BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_INT, BUSTER_TYPE_BOOL, type_pointer(table, BUSTER_TYPE_FLOAT),
                                type_atomic(table, BUSTER_TYPE_FLOAT) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ieee); i += 1)
    {
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(floating); t += 1)
        {
            arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, floating[t], BUSTER_TYPE_VOLATILE)), 21, 0);
            arguments[1] = check_test_integer(BUSTER_TYPE_INT, 3);
            arguments[2] = check_test_integer(BUSTER_TYPE_INT, 7);
            input = check_builtin_atomic_rmw_test_call(ieee[i], arguments, 3, 1u << 2);
            check_builtin_atomic_rmw_one(table, &input, &result);
            const bool valid = t < 7;
            const bool leaf = t < 2;
            check_builtin_atomic_rmw_test_expect(test, valid ? result.atomic.action.diagnostic == CHECK_DIAG_NONE && result.atomic.action.value.type == floating[t] &&
                result.operation == ieee_operations[i] && result.atomic.store_value.type == floating[t] && result.atomic.rmw && result.atomic.volatile_access &&
                result.reads_fp_environment && result.may_raise_fp_exceptions && (result.atomic.warnings & BUSTER_CHECK_BUILTIN_ATOMIC_WARN_ORDER) != 0 &&
                (leaf ? result.atomic.unsupported == 0 && result.format == (t == 0 ? CHECK_BUILTIN_ATOMIC_RMW_F32_BITS : CHECK_BUILTIN_ATOMIC_RMW_F64_BITS) :
                        result.atomic.unsupported != 0 && (result.atomic.unsupported & BUSTER_CHECK_BUILTIN_ATOMIC_RMW_UNSUPPORTED_FP_RMW) == 0) :
                result.atomic.action.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.atomic.action.diagnostic_operand == 0,
                "the IEEE maximum and minimum of floating values alone but long double, a float's and a double's with leaves", i * 16 + t);
        }
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, type_qualify(table, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_CONST)), 21, 0);
        input = check_builtin_atomic_rmw_test_call(ieee[i], arguments, 3, 1u << 2);
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "a const object's IEEE maximum is clang's error", i);
        arguments[0] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_DOUBLE), 21, 0);
        input.source.argument_count = 2;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "the IEEE maximum takes its arguments exactly", i);
        input.source.argument_count = 4;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "the IEEE maximum takes no fourth argument", i);
        arguments[1] = check_builtin_atomic_test_pointer(type_pointer(table, BUSTER_TYPE_DOUBLE), 22, 0);
        input.source.argument_count = 3;
        check_builtin_atomic_rmw_one(table, &input, &result);
        check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic != CHECK_DIAG_NONE && result.atomic.action.diagnostic_operand == 1,
            "a pointer operand doesn't convert to the floating value", i);
    }
}

static void check_builtin_atomic_rmw_test_pages(CheckBuiltinAtomicRmwTest* test, TypeTable* table)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const size_t page = page_value > 0 ? (size_t)page_value : 0;
    const size_t input_region = page != 0 ? ((BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH * sizeof(CheckBuiltinAtomicRmwAction) + 16 + page - 1) / page) * page : 0;
    const size_t output_region = page != 0 ? ((BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH * sizeof(CheckBuiltinAtomicRmwResult) + 16 + page - 1) / page) * page : 0;
    const size_t argument_region = page != 0 ? ((6 * BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH * sizeof(CheckValue) + 16 + page - 1) / page) * page : 0;
    const size_t bytes = input_region + output_region + argument_region + 3 * page;
    uint8_t* mapping = page != 0 ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    uint8_t* input_end = ready ? mapping + input_region : NULL;
    uint8_t* output_end = ready ? input_end + page + output_region : NULL;
    uint8_t* argument_end = ready ? output_end + page + argument_region : NULL;
    if (ready)
    {
        ready = mprotect(input_end, page, PROT_NONE) == 0 && mprotect(output_end, page, PROT_NONE) == 0 && mprotect(argument_end, page, PROT_NONE) == 0;
    }
    check_builtin_atomic_rmw_test_expect(test, ready, "guarded RMW action/result/fixed-argument slices", 0);
    for (uint32_t count = 0; ready && count <= BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH; count += 1)
    {
        uint32_t total = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            total += check_builtin_atomic_rmw_test_cases[i % BUSTER_CHECK_ATOMIC_RMW_TEST_CASES].fixed;
        }
        CheckBuiltinAtomicRmwAction* actions = (CheckBuiltinAtomicRmwAction*)(input_end - count * sizeof(*actions));
        CheckBuiltinAtomicRmwResult* results = (CheckBuiltinAtomicRmwResult*)(output_end - count * sizeof(*results));
        CheckValue* arguments = (CheckValue*)(argument_end - total * sizeof(*arguments));
        memset((uint8_t*)actions - 16, 0xa5, 16 + count * sizeof(*actions));
        memset((uint8_t*)results - 16, 0xa5, 16 + count * sizeof(*results));
        memset((uint8_t*)arguments - 16, 0xa5, 16 + total * sizeof(*arguments));
        uint32_t cursor = 0;
        CheckBuiltinAtomicRmwResult expected[BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH];
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t item = i % BUSTER_CHECK_ATOMIC_RMW_TEST_CASES;
            const uint32_t fixed = check_builtin_atomic_rmw_test_cases[item].fixed;
            CheckValue temporary[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
            actions[i] = check_builtin_atomic_rmw_test_input(table, item, BUSTER_TYPE_INT, temporary);
            memcpy(arguments + cursor, temporary, fixed * sizeof(*arguments));
            actions[i].source.arguments = arguments + cursor;
            actions[i].source.node = i;
            actions[i].source.alignment_known = i % 2 != 0;
            if (check_builtin_atomic_rmw_test_cases[item].legacy)
            {
                // Only the fixed prefix belongs to this module. There is deliberately no readable ignored tail.
                actions[i].source.argument_count = UINT32_MAX;
            }
            cursor += fixed;
            check_builtin_atomic_rmw_one(table, actions + i, expected + i);
        }
        CheckBuiltinAtomicRmwAction action_copy[BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH];
        CheckValue argument_copy[6 * BUSTER_CHECK_ATOMIC_RMW_TEST_BATCH];
        memcpy(action_copy, actions, count * sizeof(*actions));
        memcpy(argument_copy, arguments, total * sizeof(*arguments));
        for (uint32_t partition = 1; partition <= 7; partition += 1)
        {
            for (uint32_t first = 0; first < count; first += partition)
            {
                const uint32_t remaining = count - first < partition ? count - first : partition;
                check_builtin_atomic_rmw(table, actions + first, results + first, remaining);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_builtin_atomic_rmw_test_expect(test, check_builtin_atomic_rmw_test_equal(expected + i, results + i), "guarded legal partition preserves conversion/effect/diagnostic metadata", i);
            }
        }
        check_builtin_atomic_rmw(table, actions, results, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_builtin_atomic_rmw_test_expect(test, check_builtin_atomic_rmw_test_equal(expected + i, results + i), "guarded complete batch equals independent individual actions", i);
        }
        // By field: the actions' and the values' padding is no part of them.
        bool unchanged = true;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckBuiltinAtomicAction* x = &action_copy[i].source;
            const CheckBuiltinAtomicAction* y = &actions[i].source;
            unchanged &= x->id == y->id && x->arguments == y->arguments && x->argument_count == y->argument_count && x->node == y->node &&
                x->tag_completions == y->tag_completions && x->tag_completion_count == y->tag_completion_count && x->source_position == y->source_position &&
                x->extensions == y->extensions && x->minimum_alignment == y->minimum_alignment && x->alignment_known == y->alignment_known &&
                action_copy[i].order_ice_arguments == actions[i].order_ice_arguments;
        }
        for (uint32_t i = 0; i < total; i += 1)
        {
            unchanged &= check_test_value_equal(argument_copy + i, arguments + i);
        }
        check_builtin_atomic_rmw_test_expect(test, unchanged, "borrowed action and fixed-argument bytes remain unchanged", count);
        for (uint32_t b = 1; b <= 16; b += 1)
        {
            check_builtin_atomic_rmw_test_expect(test, *((uint8_t*)actions - b) == 0xa5 && *((uint8_t*)results - b) == 0xa5 && *((uint8_t*)arguments - b) == 0xa5,
                "RMW leading sentinels remain unchanged", b);
        }
    }
    if (ready)
    {
        for (uint32_t i = 0; i < BUSTER_CHECK_ATOMIC_RMW_TEST_CASES; i += 1)
        {
            CheckValue temporary[BUSTER_CHECK_ATOMIC_RMW_TEST_ARGUMENTS];
            CheckBuiltinAtomicRmwAction input = check_builtin_atomic_rmw_test_input(table, i, BUSTER_TYPE_INT, temporary);
            input.source.arguments = (const CheckValue*)argument_end;
            input.source.argument_count = check_builtin_atomic_rmw_test_cases[i].fixed - 1;
            CheckBuiltinAtomicRmwResult result;
            check_builtin_atomic_rmw_one(table, &input, &result);
            check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "arity check precedes protected argument read", i);
            if (!check_builtin_atomic_rmw_test_cases[i].legacy)
            {
                input.source.argument_count = check_builtin_atomic_rmw_test_cases[i].fixed + 1;
                check_builtin_atomic_rmw_one(table, &input, &result);
                check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "GNU arity is exact before protected argument read", i);
            }
        }
        const CBuiltinId unsupported[] = { CBUILTIN_NONE, CBUILTIN_ATOMIC_EXCHANGE, CBUILTIN_ATOMIC_COMPARE_EXCHANGE, CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_SYNC_SYNCHRONIZE };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsupported); i += 1)
        {
            const CheckBuiltinAtomicRmwAction input = { .source = { .id = unsupported[i], .arguments = (const CheckValue*)argument_end, .argument_count = UINT32_MAX } };
            CheckBuiltinAtomicRmwResult result;
            check_builtin_atomic_rmw_one(table, &input, &result);
            check_builtin_atomic_rmw_test_expect(test, result.atomic.action.diagnostic == CHECK_DIAG_UNSUPPORTED && result.atomic.unsupported == BUSTER_CHECK_BUILTIN_ATOMIC_UNSUPPORTED_ID,
                "other atomic families retain explicit unsupported boundary", i);
        }
    }
    check_builtin_atomic_rmw(NULL, NULL, NULL, 0);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

static bool check_builtin_atomic_rmw_test_process(char* const* arguments, int* exit_code, char diagnostics[4096])
{
    int descriptors[2];
    struct timespec started;
    *exit_code = -1;
    diagnostics[0] = 0;
    bool result = clock_gettime(CLOCK_MONOTONIC, &started) == 0 && pipe(descriptors) == 0;
    if (result)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            if (setpgid(0, 0) != 0)
            {
                _exit(125);
            }
            alarm(10);
            close(descriptors[0]);
            const int discarded = open("/dev/null", O_WRONLY);
            if (discarded < 0 || dup2(discarded, 1) < 0 || dup2(descriptors[1], 2) < 0)
            {
                _exit(125);
            }
            close(discarded);
            close(descriptors[1]);
            execvp(arguments[0], arguments);
            _exit(127);
        }
        close(descriptors[1]);
        if (child > 0)
        {
            // The child also sets its group before exec, covering either scheduling order.
            setpgid(child, child);
        }
        size_t kept = 0;
        char buffer[1024];
        bool drained = false;
        bool done = false;
        int status = 0;
        bool bounded = child > 0;
        while (bounded && (!drained || !done))
        {
            struct timespec now;
            bounded = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
            const int64_t remaining = bounded ? 10000 - ((int64_t)now.tv_sec - started.tv_sec) * 1000 - ((int64_t)now.tv_nsec - started.tv_nsec) / 1000000 : 0;
            if (!bounded || remaining <= 0)
            {
                bounded = false;
            }
            else
            {
                struct pollfd descriptor = { .fd = descriptors[0], .events = POLLIN };
                const int polled = poll(drained ? NULL : &descriptor, drained ? 0 : 1, remaining < 100 ? (int)remaining : 100);
                if (polled < 0 && errno != EINTR)
                {
                    bounded = false;
                }
                if (polled > 0 && !drained)
                {
                    const ssize_t bytes = read(descriptors[0], buffer, sizeof(buffer));
                    if (bytes > 0)
                    {
                        const size_t copied = (size_t)bytes < 4095 - kept ? (size_t)bytes : 4095 - kept;
                        memcpy(diagnostics + kept, buffer, copied);
                        kept += copied;
                    }
                    else if (bytes == 0)
                    {
                        drained = true;
                    }
                    else if (errno != EINTR)
                    {
                        bounded = false;
                    }
                }
                if (!done)
                {
                    const pid_t waited = waitpid(child, &status, WNOHANG);
                    done = waited == child;
                    if (waited < 0 && errno != EINTR)
                    {
                        bounded = false;
                    }
                }
            }
        }
        diagnostics[kept] = 0;
        close(descriptors[0]);
        if (!bounded && child > 0)
        {
            // A compiler driver may spawn a linker that retains stderr. Terminate the whole group at the parent deadline.
            kill(-child, SIGKILL);
            if (!done)
            {
                kill(child, SIGKILL);
                pid_t waited;
                do
                {
                    waited = waitpid(child, &status, 0);
                }
                while (waited < 0 && errno == EINTR);
            }
        }
        result = bounded && drained && done && WIFEXITED(status) && WEXITSTATUS(status) < 125;
        *exit_code = result ? WEXITSTATUS(status) : -1;
    }
    return result;
}

// Cache only the independent compiler. Execution of its generated program uses the direct runner above.
static bool check_builtin_atomic_rmw_test_clang_process(char* const* arguments, const char* artifact, int* exit_code, char diagnostics[4096])
{
    const char* files[] = { artifact };
    OracleRun run = {
        .purpose = "checker atomic RMW Clang oracle",
        .arguments = arguments,
        .files = files,
        .file_count = artifact != 0,
        .dependencies = true,
        .timeout = 10,
    };
    *exit_code = oracle_test_run(&run, 0, 0, diagnostics, 4096);
    return *exit_code >= 0 && *exit_code < 125;
}

#define BUSTER_ATOMIC_RMW_TEST_STRINGIFY_(value) #value
#define BUSTER_ATOMIC_RMW_TEST_STRINGIFY(value) BUSTER_ATOMIC_RMW_TEST_STRINGIFY_(value)

static void check_builtin_atomic_rmw_test_clang(CheckBuiltinAtomicRmwTest* test, const char* clang)
{
    static const char version_source[] =
        "#if !defined(__clang__) || __clang_major__ != " BUSTER_ATOMIC_RMW_TEST_STRINGIFY(__clang_major__)
        " || __clang_minor__ != " BUSTER_ATOMIC_RMW_TEST_STRINGIFY(__clang_minor__)
        " || __clang_patchlevel__ != " BUSTER_ATOMIC_RMW_TEST_STRINGIFY(__clang_patchlevel__) "\n"
        "#error external oracle must match the build Clang version\n"
        "#endif\n";
    static const struct
    {
        const char* source;
        const char* warning;
        bool rejected;
    } cases[] =
    {
        { "volatile unsigned long x; _Static_assert(_Generic(__atomic_exchange_n(&x,1,5),unsigned long:1,default:0),\"type\");\n", NULL, false },
        { "_Bool f(_Bool*p){return __atomic_add_fetch(p,2,5);}\n", NULL, false },
        { "_Bool f(_Bool*p){return __atomic_nand_fetch(p,1,5);}\n", NULL, false },
        { "float f(float*p){return __atomic_exchange_n(p,3.75,5);}\n", NULL, false },
        { "double f(double*p){return __atomic_add_fetch(p,3.75,5);}\n", NULL, false },
        { "_Bool f(double*p,double*e){return __atomic_compare_exchange_n(p,e,1,0,5,2);}\n", NULL, true },
        { "long double f(long double*p){return __atomic_exchange_n(p,1,5);}\n", NULL, true },
        { "int*f(int**p){return __atomic_fetch_add(p,4,5);}\n", NULL, false },
        { "int*f(int**p){return __atomic_fetch_and(p,4,5);}\n", NULL, true },
        { "int*f(int**p,int*v){return __sync_fetch_and_add(p,v);}\n", NULL, false },
        { "int*f(int**p,int*v){return __sync_fetch_and_and(p,v);}\n", NULL, false },
        { "_Bool f(int*p,int*e,int w,int s,int f){return __atomic_compare_exchange_n(p,e,1,w++,s++,f++);}\n", NULL, false },
        { "_Bool f(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,2,0,5);}\n", "-Werror=atomic-memory-ordering", false },
        { "_Bool f(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,0,6,2);}\n", "-Werror=atomic-memory-ordering", true },
        { "_Bool f(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,0,5,3);}\n", "-Werror=atomic-memory-ordering", true },
        { "_Bool f(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,0,5,4);}\n", "-Werror=atomic-memory-ordering", true },
        { "_Bool f(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,0,4294967296ULL,5);}\n", "-Werror=atomic-memory-ordering", false },
        { "_Bool f(int*p,const int*e){return __atomic_compare_exchange_n(p,e,1,0,5,2);}\n", NULL, false },
        { "_Bool f(int*p,const int*e){return __atomic_compare_exchange_n(p,e,1,0,5,2);}\n", "-Werror=incompatible-pointer-types-discards-qualifiers", true },
        { "_Bool f(int*p,unsigned int*e){return __atomic_compare_exchange_n(p,e,1,0,5,2);}\n", "-Werror=pointer-sign", true },
        { "_Bool f(int*p,double*e){return __atomic_compare_exchange_n(p,e,1,0,5,2);}\n", NULL, true },
        { "int f(const int*p){return __atomic_exchange_n(p,1,5);}\n", NULL, true },
        { "unsigned _BitInt(8) f(unsigned _BitInt(8)*p){return __atomic_fetch_add(p,1,5);}\n", NULL, true },
        { "unsigned _BitInt(4) f(unsigned _BitInt(4)*p){return __sync_fetch_and_add(p,1);}\n", NULL, false },
        { "unsigned _BitInt(24) f(unsigned _BitInt(24)*p){return __sync_fetch_and_add(p,1);}\n", NULL, true },
        { "unsigned _BitInt(128) f(unsigned _BitInt(128)*p){return __sync_fetch_and_add(p,1);}\n", NULL, false },
        { "unsigned _BitInt(256) f(unsigned _BitInt(256)*p){return __sync_fetch_and_add(p,1);}\n", NULL, true },
        { "int f(int*p,unsigned _BitInt(256)v){return __atomic_exchange_n(p,v,5);}\n", NULL, false },
        { "int f(int*p,int*q){return __sync_fetch_and_add(p,1,++*q,1/0);}\n", NULL, false },
        { "int f(int*p){return __sync_fetch_and_add(p,1,unknown148);}\n", NULL, true },
        { "_Bool f(_Bool*p){return __sync_fetch_and_nand(p,1);}\n", "-Werror=sync-fetch-and-nand-semantics-changed", true },
        { "void f(int*p){__sync_lock_release(p);}\n", "-Werror=atomic-implicit-seq-cst", true },
        { "int f(int*p){return __atomic_exchange_n(p,1,5,0);}\n", NULL, true },
        { "int f(int*p){return __sync_fetch_and_add(p);}\n", NULL, true },
        { "unsigned f(unsigned*p){return __atomic_fetch_min(p,1,5)+__atomic_max_fetch(p,1,0);}\n", NULL, false },
        { "float f(float*p){return __atomic_min_fetch(p,1,5);}\n", NULL, false },
        { "_Bool f(_Bool*p){return __atomic_fetch_max(p,1,5);}\n", NULL, false },
        { "int*f(int**p){return __atomic_fetch_min(p,0,5);}\n", NULL, true },
        { "long double f(long double*p){return __atomic_fetch_max(p,1,5);}\n", NULL, true },
        { "unsigned _BitInt(32) f(unsigned _BitInt(32)*p){return __atomic_fetch_min(p,1,5);}\n", NULL, true },
        { "int f(int*p){return __atomic_fetch_min(p,1);}\n", NULL, true },
    };
    static const char native_source[] =
        "static int bad(void){__builtin_trap();return 7;}\n"
        "int main(void){int x=7,ignored=0;int old=__sync_fetch_and_add(&x,1,bad(),ignored++);if(old!=7||x!=8||ignored)return 1;"
        "if(!__sync_bool_compare_and_swap(&x,8,9,bad(),++ignored)||x!=9||ignored)return 2;__sync_lock_release(&x,bad(),ignored++);if(x||ignored)return 3;"
        "int e=-1,pc=0,ec=0,dc=0,wc=0,sc=0,fc=0;_Bool ok=__atomic_compare_exchange_n((pc++,&x),(ec++,&e),(dc++,11),(wc++,0),(sc++,5),(fc++,2));"
        "if(ok||x||e||pc!=1||ec!=1||dc!=1||wc!=1||sc!=1||fc!=1)return 4;"
        "ok=__atomic_compare_exchange_n((pc++,&x),(ec++,&e),(dc++,11),(wc++,0),(sc++,5),(fc++,2));"
        "if(!ok||x!=11||e||pc!=2||ec!=2||dc!=2||wc!=2||sc!=2||fc!=2)return 5;"
        "int a[8];int*p=a;int*r=__atomic_fetch_add(&p,4,5);if(r!=a||(char*)p-(char*)a!=4)return 6;"
        "p=a;r=__sync_fetch_and_add(&p,(int*)4);if(r!=a||(char*)p-(char*)a!=4)return 7;"
        "_Bool b=0;if(!__atomic_or_fetch(&b,2,5)||!b)return 8;return 0;}\n";
    char directory[] = "/tmp/buster-check-rmw-XXXXXX";
    const bool ready = mkdtemp(directory) != NULL;
    check_builtin_atomic_rmw_test_expect(test, ready, "external Clang fixture directory", 0);
    bool qualified = false;
    if (ready)
    {
        char path[160];
        snprintf(path, sizeof(path), "%s/version.c", directory);
        char* arguments[] = { (char*)clang, "-fsyntax-only", "-std=gnu17", path, NULL };
        char diagnostics[4096] = { 0 };
        int exit_code = -1;
        qualified = parse_test_write(path, version_source, sizeof(version_source) - 1) &&
            check_builtin_atomic_rmw_test_clang_process(arguments, 0, &exit_code, diagnostics) && exit_code == 0;
        check_builtin_atomic_rmw_test_expect(test, qualified, "external oracle matches the build Clang version", 0);
        if (!qualified && test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-atomic-rmw: Clang version gate: %s\n", diagnostics);
        }
        unlink(path);
    }
    for (uint32_t i = 0; qualified && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char path[160];
        snprintf(path, sizeof(path), "%s/%u.c", directory, i);
        bool ran = parse_test_write(path, cases[i].source, strlen(cases[i].source));
        char* arguments[10] = { (char*)clang, "-fsyntax-only", "-std=gnu17", "-fno-color-diagnostics", "-fno-caret-diagnostics", "-ferror-limit=0" };
        uint32_t count = 6;
        if (cases[i].warning != NULL)
        {
            arguments[count++] = (char*)cases[i].warning;
        }
        arguments[count++] = path;
        arguments[count] = NULL;
        char diagnostics[4096] = { 0 };
        int exit_code = -1;
        ran = ran && check_builtin_atomic_rmw_test_clang_process(arguments, 0, &exit_code, diagnostics);
        const bool matched = ran && (exit_code != 0) == cases[i].rejected;
        if (!matched && test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-atomic-rmw: Clang fixture%u exit%d expected-reject%u: %s\n", i, exit_code, (unsigned)cases[i].rejected, diagnostics);
        }
        check_builtin_atomic_rmw_test_expect(test, matched, "independent exact Clang type/order/warning/source constraint", i);
        unlink(path);
    }
    if (qualified)
    {
        char source[160];
        char program[160];
        char object[160];
        snprintf(source, sizeof(source), "%s/native.c", directory);
        snprintf(program, sizeof(program), "%s/native", directory);
        snprintf(object, sizeof(object), "%s/native.o", directory);
        char* compiler_arguments[] = { (char*)clang, "-std=gnu17", "-O2", "-march=x86-64", "-fno-strict-aliasing", "-c", "-o", object, source, NULL };
        char* link_arguments[] = { (char*)clang, "-march=x86-64", "-o", program, object, NULL };
        char diagnostics[4096] = { 0 };
        int exit_code = -1;
        const bool built = parse_test_write(source, native_source, sizeof(native_source) - 1) &&
                           check_builtin_atomic_rmw_test_clang_process(compiler_arguments, object, &exit_code, diagnostics) && exit_code == 0 &&
                           check_builtin_atomic_rmw_test_process(link_arguments, &exit_code, diagnostics) && exit_code == 0;
        check_builtin_atomic_rmw_test_expect(test, built, "independent native expression-evaluation contract oracle built", 0);
        if (built)
        {
            char* arguments[] = { program, NULL };
            const bool ran = check_builtin_atomic_rmw_test_process(arguments, &exit_code, diagnostics);
            check_builtin_atomic_rmw_test_expect(test, ran && exit_code == 0, "Clang ignores legacy tails, evaluates GNU fixed operands once and uses raw pointer byte deltas", (uint32_t)exit_code);
        }
        else if (test->failures < 24)
        {
            fprintf(stderr, "test-check-builtins-atomic-rmw: native oracle build: %s\n", diagnostics);
        }
        unlink(source);
        unlink(program);
        unlink(object);
    }
    if (ready)
    {
        rmdir(directory);
    }
}

#undef BUSTER_ATOMIC_RMW_TEST_STRINGIFY
#undef BUSTER_ATOMIC_RMW_TEST_STRINGIFY_

static int run_check_builtin_atomic_rmw_tests(int argc, char** argv)
{
    const bool epoch = oracle_epoch_begin();
    CheckBuiltinAtomicRmwTest test = { 0 };
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
    check_builtin_atomic_rmw_test_expect(&test, options, "usage: test-check-builtins-atomic-rmw [--clang PATH]", 0);
    CheckTestTypes types = { 0 };
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_builtin_atomic_rmw_test_expect(&test, initialized, "type-table allocation", 0);
        if (initialized)
        {
            check_builtin_atomic_rmw_test_values(&test, &types.table);
            check_builtin_atomic_rmw_test_constraints(&test, &types.table);
            check_builtin_atomic_rmw_test_orders(&test, &types.table);
            check_builtin_atomic_rmw_test_added(&test, &types.table);
            check_builtin_atomic_rmw_test_pages(&test, &types.table);
        }
        check_test_types_close(&types);
        check_builtin_atomic_rmw_test_clang(&test, clang);
    }
    printf("test-check-builtins-atomic-rmw: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    oracle_cache_report(oracle_cache(), "test-check-builtins-atomic-rmw");
    oracle_epoch_end(epoch);
    return test.failures != 0;
}
