// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Semantic-action fixtures use independently stated C results, and graph tests compare the postorder and level
// schedulers field by field. Floating padding is never compared. The generated actions exercise target arithmetic,
// deferred faults, protected tails and legal batch partitions; source fixtures additionally test the parser adapter.
// Include after c_checker.c and c_check_input.c in the compiler's test unit.

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_CHECK_TEST_BATCH 257u

typedef struct CheckTest CheckTest;
struct CheckTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

typedef struct CheckTestTypes CheckTestTypes;
struct CheckTestTypes
{
    TypeTable table;
    Arena arena;
    Arena index;
};

static bool check_test_types_open_with(CheckTestTypes* types, TypeCapacity capacity)
{
    types->arena = arena_reserve(type_capacity_bytes(capacity));
    types->index = arena_reserve(type_index_capacity_bytes(capacity));
    return type_initialize(&types->table, &types->arena, &types->index, capacity);
}

static bool check_test_types_open(CheckTestTypes* types)
{
    const TypeCapacity capacity = {
        .types = 4096,
        .parameters = 8192,
        .tags = 512,
        .members = 4096,
    };
    return check_test_types_open_with(types, capacity);
}

static __attribute__((noinline)) void check_test_types_close(CheckTestTypes* types)
{
    arena_release(&types->index);
    arena_release(&types->arena);
}

static __attribute__((noinline)) void check_test_expect(CheckTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 24)
        {
            fprintf(stderr, "test-checker: %s\n", description);
        }
    }
}

static uint32_t check_test_random(CheckTest* test)
{
    test->random = test->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(test->random >> 32);
}

static CheckValue check_test_integer(TypeId type, unsigned __int128 bits)
{
    const CheckValue result = {
        .type = type,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE,
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = bits,
    };
    return result;
}

static CheckValue check_test_float(TypeId type, long double value)
{
    const CheckValue result = {
        .type = type,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_FLOAT_LITERAL,
        .constant_kind = CHECK_CONSTANT_FLOATING,
        .floating = value,
    };
    return result;
}

static bool check_test_value_equal(const CheckValue* a, const CheckValue* b)
{
    const bool floating = (isnan(a->floating) && isnan(b->floating)) ||
                          (a->floating == b->floating && signbit(a->floating) == signbit(b->floating));
    const bool imaginary = (isnan(a->imaginary) && isnan(b->imaginary)) ||
                           (a->imaginary == b->imaginary && signbit(a->imaginary) == signbit(b->imaginary));
    return a->type == b->type && a->category == b->category && a->flags == b->flags &&
           a->constant_kind == b->constant_kind && a->integer == b->integer && floating && imaginary && a->object == b->object && a->overload_head == b->overload_head &&
           a->offset == b->offset && a->bit_width == b->bit_width && a->fault == b->fault && a->fault_node == b->fault_node;
}

static bool check_test_action_equal(const CheckActionResult* a, const CheckActionResult* b)
{
    bool equal = check_test_value_equal(&a->value, &b->value) && a->diagnostic == b->diagnostic && a->warnings == b->warnings &&
                 a->diagnostic_operand == b->diagnostic_operand && a->store_conversion.type == b->store_conversion.type &&
                 a->store_conversion.flags == b->store_conversion.flags;
    for (uint32_t i = 0; equal && i < 3; i += 1)
    {
        equal = a->conversions[i].type == b->conversions[i].type && a->conversions[i].flags == b->conversions[i].flags;
    }
    return equal;
}

static CheckActionResult check_test_action(CheckTest* test, TypeTable* table, CheckAction action)
{
    CheckActionResult scalar;
    CheckAction actions[8];
    CheckActionResult vector[8];
    memset(&scalar, 0xa5, sizeof(scalar));
    memset(vector, 0x5a, sizeof(vector));
    check_action_scalar(table, &action, &scalar);
    const uint32_t count = action.op == CHECK_OP_CALL ? 1 : BUSTER_ARRAY_LENGTH(actions);
    for (uint32_t i = 0; i < count; i += 1)
    {
        actions[i] = action;
    }
    check_actions(table, actions, vector, count);
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_test_expect(test, check_test_action_equal(&scalar, &vector[i]), "independent fixture differs in a full action batch");
    }
    return scalar;
}

static void check_test_binary_integer(CheckTest* test, TypeTable* table, const char* description, CheckOp op, TypeId at, unsigned __int128 av,
                                      TypeId bt, unsigned __int128 bv, TypeId expected_type, unsigned __int128 expected, CheckFault fault)
{
    const CheckAction action = {
        .op = op,
        .operands = {
            check_test_integer(at, av),
            check_test_integer(bt, bv),
        },
        .node = 71,
    };
    const CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == expected_type && result.value.fault == fault &&
                           (fault != CHECK_FAULT_NONE || (result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == expected)), description);
    if (fault != CHECK_FAULT_NONE)
    {
        check_test_expect(test, result.value.fault_node == action.node, "fault lost its source node");
    }
}

static __attribute__((noinline)) void check_test_action_fixtures(CheckTest* test, TypeTable* table)
{
    const unsigned __int128 high128 = (unsigned __int128)1 << 127;
    const unsigned __int128 mask128 = ~(unsigned __int128)0;
    check_test_binary_integer(test, table, "int addition", CHECK_OP_ADD, BUSTER_TYPE_INT, 19, BUSTER_TYPE_INT, 23, BUSTER_TYPE_INT, 42, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "unsigned 32-bit wrap", CHECK_OP_ADD, BUSTER_TYPE_UINT, UINT32_MAX, BUSTER_TYPE_UINT, 1, BUSTER_TYPE_UINT, 0, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "unsigned 64-bit wrap", CHECK_OP_ADD, BUSTER_TYPE_ULONG, UINT64_MAX, BUSTER_TYPE_ULONG, 1, BUSTER_TYPE_ULONG, 0, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "unsigned 128-bit wrap", CHECK_OP_ADD, BUSTER_TYPE_UINT128, mask128, BUSTER_TYPE_UINT128, 1, BUSTER_TYPE_UINT128, 0, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "signed 32-bit addition overflow", CHECK_OP_ADD, BUSTER_TYPE_INT, INT32_MAX, BUSTER_TYPE_INT, 1, BUSTER_TYPE_INT, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "signed 64-bit addition overflow", CHECK_OP_ADD, BUSTER_TYPE_LONG, INT64_MAX, BUSTER_TYPE_LONG, 1, BUSTER_TYPE_LONG, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "signed 128-bit addition overflow", CHECK_OP_ADD, BUSTER_TYPE_INT128, high128 - 1, BUSTER_TYPE_INT128, 1, BUSTER_TYPE_INT128, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "signed 128-bit subtraction overflow", CHECK_OP_SUBTRACT, BUSTER_TYPE_INT128, high128, BUSTER_TYPE_INT128, 1, BUSTER_TYPE_INT128, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "signed 128-bit minimum divided by -1", CHECK_OP_DIVIDE, BUSTER_TYPE_INT128, high128, BUSTER_TYPE_INT128, mask128, BUSTER_TYPE_INT128, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "integer division by zero", CHECK_OP_DIVIDE, BUSTER_TYPE_INT, 9, BUSTER_TYPE_INT, 0, BUSTER_TYPE_INT, 0, CHECK_FAULT_DIVIDE_BY_ZERO);
    check_test_binary_integer(test, table, "integer remainder by zero", CHECK_OP_REMAINDER, BUSTER_TYPE_INT, 9, BUSTER_TYPE_INT, 0, BUSTER_TYPE_INT, 0, CHECK_FAULT_DIVIDE_BY_ZERO);
    check_test_binary_integer(test, table, "negative quotient truncates toward zero", CHECK_OP_DIVIDE, BUSTER_TYPE_INT, UINT32_C(0xfffffff9), BUSTER_TYPE_INT, 3, BUSTER_TYPE_INT, UINT32_C(0xfffffffe), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "negative remainder has dividend sign", CHECK_OP_REMAINDER, BUSTER_TYPE_INT, UINT32_C(0xfffffff9), BUSTER_TYPE_INT, 3, BUSTER_TYPE_INT, UINT32_MAX, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "long represents every unsigned int", CHECK_OP_ADD, BUSTER_TYPE_LONG, UINT64_MAX, BUSTER_TYPE_UINT, UINT32_MAX, BUSTER_TYPE_LONG, UINT64_C(4294967294), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "unsigned long dominates long long", CHECK_OP_ADD, BUSTER_TYPE_ULONG, 1, BUSTER_TYPE_LLONG, UINT64_MAX, BUSTER_TYPE_ULLONG, 0, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "short integer promotions", CHECK_OP_ADD, BUSTER_TYPE_SHORT, UINT16_MAX, BUSTER_TYPE_USHORT, UINT16_MAX, BUSTER_TYPE_INT, 65534, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "mixed signed unsigned comparison", CHECK_OP_LESS, BUSTER_TYPE_INT, UINT32_MAX, BUSTER_TYPE_UINT, 1, BUSTER_TYPE_INT, 0, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "signed comparison", CHECK_OP_LESS, BUSTER_TYPE_LONG, UINT64_MAX, BUSTER_TYPE_LONG, 1, BUSTER_TYPE_INT, 1, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "shift promotes only each operand", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_UCHAR, 1, BUSTER_TYPE_ULONG, 3, BUSTER_TYPE_INT, 8, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "unsigned shift wraps", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_UINT, UINT32_MAX, BUSTER_TYPE_INT, 1, BUSTER_TYPE_UINT, UINT32_C(0xfffffffe), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "shift count equal to width", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, 1, BUSTER_TYPE_INT, 32, BUSTER_TYPE_INT, 0, CHECK_FAULT_SHIFT_COUNT);
    check_test_binary_integer(test, table, "negative shift count", CHECK_OP_SHIFT_RIGHT, BUSTER_TYPE_INT, 1, BUSTER_TYPE_INT, UINT32_MAX, BUSTER_TYPE_INT, 0, CHECK_FAULT_SHIFT_COUNT);
    check_test_binary_integer(test, table, "signed right shift follows target", CHECK_OP_SHIFT_RIGHT, BUSTER_TYPE_INT, UINT32_C(0xfffffff8), BUSTER_TYPE_INT, 2, BUSTER_TYPE_INT, UINT32_C(0xfffffffe), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "bitwise complement operands are width bounded", CHECK_OP_BIT_XOR, BUSTER_TYPE_UINT, UINT32_MAX, BUSTER_TYPE_UINT, UINT32_C(0xff), BUSTER_TYPE_UINT, UINT32_C(0xffffff00), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "wide multiplication", CHECK_OP_MULTIPLY, BUSTER_TYPE_UINT128, (unsigned __int128)1 << 80, BUSTER_TYPE_UINT128, (unsigned __int128)1 << 32, BUSTER_TYPE_UINT128, (unsigned __int128)1 << 112, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "signed wide multiplication overflow", CHECK_OP_MULTIPLY, BUSTER_TYPE_INT128, (unsigned __int128)1 << 100, BUSTER_TYPE_INT128, (unsigned __int128)1 << 40, BUSTER_TYPE_INT128, 0, CHECK_FAULT_SIGNED_OVERFLOW);
    check_test_binary_integer(test, table, "signed 32-bit comparison ignores high input bits", CHECK_OP_LESS, BUSTER_TYPE_INT, mask128, BUSTER_TYPE_INT, 1,
                              BUSTER_TYPE_INT, 1, CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "signed 32-bit sum ignores high input bits", CHECK_OP_ADD, BUSTER_TYPE_INT, mask128, BUSTER_TYPE_INT, mask128,
                              BUSTER_TYPE_INT, UINT32_C(0xfffffffe), CHECK_FAULT_NONE);

    CheckAction action = {
        .op = CHECK_OP_DIVIDE,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, 1),
            check_test_integer(BUSTER_TYPE_INT, 0),
        },
        .node = 94,
    };
    const CheckActionResult divide = check_test_action(test, table, action);
    action.op = CHECK_OP_LOGICAL_AND;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    action.operands[1] = divide.value;
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE && result.value.integer == 0 &&
                           (result.value.flags & BUSTER_CHECK_ICE) != 0, "0 && (1 / 0) is a valid unevaluated constant branch");
    action.op = CHECK_OP_LOGICAL_OR;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.fault == CHECK_FAULT_NONE && result.value.integer == 1 && (result.value.flags & BUSTER_CHECK_ICE) != 0,
                      "1 || (1 / 0) discards unselected evaluation fault");
    action.op = CHECK_OP_LOGICAL_AND;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.value.fault_node == 94, "selected logical operand preserves fault and origin");
    action.op = CHECK_OP_CONDITIONAL;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 0);
    action.operands[1] = divide.value;
    action.operands[2] = check_test_integer(BUSTER_TYPE_INT, 17);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.fault == CHECK_FAULT_NONE && result.value.integer == 17 && (result.value.flags & BUSTER_CHECK_ICE) != 0,
                      "conditional discards the unselected arm's fault");
    action = (CheckAction){
        .op = CHECK_OP_SIZEOF,
        .operands = {
            divide.value
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_ULONG && result.value.integer == 4 &&
                           result.value.fault == CHECK_FAULT_NONE && (result.value.flags & BUSTER_CHECK_ICE) != 0, "sizeof suppresses evaluation faults");

    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, 257)
        },
        .type = BUSTER_TYPE_UCHAR
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.type == BUSTER_TYPE_UCHAR && result.value.integer == 1 &&
                           (result.conversions[0].flags & BUSTER_CHECK_CONVERT_CAST) != 0, "explicit narrowing conversion keeps target bits");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .operands = {
            check_test_float(BUSTER_TYPE_DOUBLE, 3.75L)
        },
        .type = BUSTER_TYPE_INT
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 3 && (result.value.flags & BUSTER_CHECK_ICE) != 0,
                      "immediate floating literal cast is an integer constant expression");
    action = (CheckAction){
        .op = CHECK_OP_ADD,
        .operands = {
            check_test_float(BUSTER_TYPE_FLOAT, 1.5L),
            check_test_float(BUSTER_TYPE_DOUBLE, 2.25L)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.type == BUSTER_TYPE_DOUBLE && result.value.constant_kind == CHECK_CONSTANT_FLOATING && result.value.floating == 3.75L,
                      "floating arithmetic applies the common type");
    action = (CheckAction){
        .op = CHECK_OP_REMAINDER,
        .operands = {
            check_test_float(BUSTER_TYPE_FLOAT, 1),
            check_test_integer(BUSTER_TYPE_INT, 2)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INTEGER_REQUIRED, "remainder rejects floating operands");

    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = pointer,
                .category = CHECK_LVALUE
            },
            check_test_integer(BUSTER_TYPE_INT, 0)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == pointer &&
                           (result.conversions[1].flags & BUSTER_CHECK_CONVERT_NULL) != 0, "assignment recognizes a null pointer constant");
    action.operands[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE, "integer one is not a null pointer constant");
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = BUSTER_TYPE_INT | BUSTER_TYPE_CONST,
                .category = CHECK_LVALUE
            },
            check_test_integer(BUSTER_TYPE_INT, 1)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "assignment rejects a const lvalue");
    action = (CheckAction){
        .op = CHECK_OP_ADDRESS,
        .operands = {
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .flags = BUSTER_CHECK_BIT_FIELD,
                .bit_width = 3
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_BIT_FIELD, "address of a bit-field is a constraint violation");
    action.operands[0].flags = BUSTER_CHECK_REGISTER;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_REGISTER_ADDRESS, "address of a register object is a constraint violation");
    const TypeId array = type_array(table, BUSTER_TYPE_INT, 7);
    action = (CheckAction){
        .op = CHECK_OP_SIZEOF,
        .operands = {
            {
                .type = array,
                .category = CHECK_LVALUE
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.integer == 28 && result.conversions[0].flags == 0, "sizeof preserves array type without decay");
    action.op = CHECK_OP_ADD;
    action.operands[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == pointer &&
                           (result.conversions[0].flags & BUSTER_CHECK_CONVERT_ARRAY) != 0, "pointer arithmetic explicitly decays an array");
}

static __attribute__((noinline)) void check_test_action_constraints(CheckTest* test, TypeTable* table)
{
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_INT);
    const TypeId const_pointer = type_pointer(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST);
    const TypeId double_pointer = type_pointer(table, pointer);
    const TypeId double_const_pointer = type_pointer(table, const_pointer);
    CheckAction action = {
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = const_pointer,
                .category = CHECK_LVALUE
            },
            {
                .type = pointer,
                .category = CHECK_RVALUE
            },
        },
    };
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.conversions[1].type == const_pointer, "one-level pointer qualification is allowed");
    // clang takes both of these in every language mode, with -Wincompatible-pointer-types-discards-qualifiers.
    action.operands[0].type = double_const_pointer;
    action.operands[1].type = double_pointer;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_DISCARDED_QUALIFIERS && result.conversions[1].type == double_const_pointer,
                      "pointer qualification through a second pointer warns rather than refuses");
    action.operands[0].type = pointer;
    action.operands[1].type = const_pointer;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_DISCARDED_QUALIFIERS, "pointer assignment discarding pointed-to const warns");
    action.operands[0].type = type_pointer(table, type_pointer(table, BUSTER_TYPE_CHAR));
    action.operands[1].type = double_pointer;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPATIBLE && result.warnings == 0, "pointers to pointers to other types remain incompatible");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .type = BUSTER_TYPE_ULONG,
        .operands = {
            check_test_integer(BUSTER_TYPE_SCHAR, UINT8_MAX)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.integer == UINT64_MAX, "negative narrow integer widens before unsigned conversion");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .type = BUSTER_TYPE_INT,
        .operands = {
            check_test_float(BUSTER_TYPE_DOUBLE, 1e30L)
        },
        .node = 93
    };
    result = check_test_action(test, table, action);
    // Undefined, which clang's folding saturates: a GNU constant, not an ICE, that no array bound takes.
    check_test_expect(test, result.value.fault == CHECK_FAULT_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == INT32_MAX &&
                      (result.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM)) == 0 &&
                      (result.value.flags & (BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_FLOAT_RANGE)) == (BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_FLOAT_RANGE),
                      "out-of-range floating conversion saturates as clang folds it");
    const long double saturated[] = { -1e30L, __builtin_nanl(""), __builtin_infl(), -__builtin_infl(), 0x1p31L, -0x1p31L - 1 };
    const TypeId saturating[] = { BUSTER_TYPE_INT, BUSTER_TYPE_UINT, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_LONG };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(saturating); t += 1)
    {
        uint32_t width = 0;
        bool sign = false;
        check_action_integer_type(table, saturating[t], &width, &sign);
        const unsigned __int128 mask = width == 128 ? ~(unsigned __int128)0 : ((unsigned __int128)1 << width) - 1;
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(saturated); v += 1)
        {
            const long double input = saturated[v];
            // Independently: in range truncates; a NaN is 0; else the nearer of the type's two ends.
            const long double low = sign ? -(long double)((unsigned __int128)1 << (width - 1)) : 0;
            const long double high = sign ? (long double)((unsigned __int128)1 << (width - 1)) : (long double)((unsigned __int128)1 << (width - 1)) * 2;
            const bool range = input == input && input > low - 1 && input < high;
            const unsigned __int128 expected = range ? (unsigned __int128)(__int128)input & mask : input != input ? 0 :
                                               input < 0 ? (sign ? (unsigned __int128)1 << (width - 1) : 0) : (sign ? ((unsigned __int128)1 << (width - 1)) - 1 : mask);
            action = (CheckAction){ .op = CHECK_OP_CAST, .type = saturating[t], .operands = { check_test_float(BUSTER_TYPE_LONG_DOUBLE, input) }, .node = 94 };
            result = check_test_action(test, table, action);
            check_test_expect(test, result.value.fault == CHECK_FAULT_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER &&
                              result.value.integer == expected && ((result.value.flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0) == !range,
                              "floating conversion to each integer width saturates past its range and truncates in it");
        }
    }
    action = (CheckAction){
        .op = CHECK_OP_ADD,
        .operands = {
            check_test_float(BUSTER_TYPE_FLOAT, 16777216.0L),
            check_test_float(BUSTER_TYPE_FLOAT, 1.0L)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.type == BUSTER_TYPE_FLOAT && result.value.floating == 16777216.0L, "float operation rounds to target precision");
    action = (CheckAction){
        .op = CHECK_OP_EQUAL,
        .operands = {
            check_test_float(BUSTER_TYPE_DOUBLE, NAN),
            check_test_float(BUSTER_TYPE_DOUBLE, NAN)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.integer == 0, "NaN compares unequal");
    action.op = CHECK_OP_NOT_EQUAL;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.integer == 1, "NaN inequality is true");
    check_test_binary_integer(test, table, "signed shift into sign bit", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, 1, BUSTER_TYPE_INT, 31,
                              BUSTER_TYPE_INT, UINT32_C(0x80000000), CHECK_FAULT_NONE);
    check_test_binary_integer(test, table, "signed shift discarding bits", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, 2, BUSTER_TYPE_INT, 31,
                              BUSTER_TYPE_INT, 0, CHECK_FAULT_SHIFT_VALUE);
    check_test_binary_integer(test, table, "left shift of negative value", CHECK_OP_SHIFT_LEFT, BUSTER_TYPE_INT, UINT32_MAX, BUSTER_TYPE_INT, 1,
                              BUSTER_TYPE_INT, 0, CHECK_FAULT_SHIFT_VALUE);
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 71);
    check_test_expect(test, type_enum_complete(table, enumeration, BUSTER_TYPE_UCHAR, 0), "enum fixture construction");
    action = (CheckAction){
        .op = CHECK_OP_ADD,
        .operands = {
            check_test_integer(enumeration, 255),
            check_test_integer(BUSTER_TYPE_INT, 1)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.type == BUSTER_TYPE_INT && result.value.integer == 256, "enumeration receives integer promotion");

    const TypeId parameters[] = {
        BUSTER_TYPE_INT,
        BUSTER_TYPE_DOUBLE
    };
    const TypeId function = type_function(table, BUSTER_TYPE_LONG, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC);
    const CheckValue arguments[] = {
        check_test_integer(BUSTER_TYPE_SHORT, 3),
        check_test_float(BUSTER_TYPE_FLOAT, 1.25L),
        check_test_float(BUSTER_TYPE_FLOAT, 2.5L),
    };
    CheckConversion conversions[3] = { 0 };
    action = (CheckAction){
        .op = CHECK_OP_CALL,
        .operands = {
            {
                .type = function,
                .category = CHECK_FUNCTION,
                .flags = BUSTER_CHECK_STATIC_OBJECT
            }
        },
        .arguments = arguments,
        .argument_count = 3,
        .argument_conversions = conversions,
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_LONG &&
                           conversions[0].type == BUSTER_TYPE_INT && conversions[1].type == BUSTER_TYPE_DOUBLE && conversions[2].type == BUSTER_TYPE_DOUBLE,
                      "prototype and variadic argument conversions are explicit");
    check_test_expect(test, (result.conversions[0].flags & BUSTER_CHECK_CONVERT_FUNCTION) != 0, "calling a function designator records its decay");
    const CheckAction logical = {
        .op = CHECK_OP_LOGICAL_OR,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, 1),
            result.value
        },
    };
    const CheckActionResult unevaluated_call = check_test_action(test, table, logical);
    check_test_expect(test, unevaluated_call.diagnostic == CHECK_DIAG_NONE && unevaluated_call.value.integer == 1 &&
                           (unevaluated_call.value.flags & BUSTER_CHECK_ICE) == 0, "strict ICE rules do not implicitly enable GNU folded-constant extensions");
    action.argument_count = 1;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_ARGUMENT_COUNT, "prototype missing argument rejected");

    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 81);
    const TypeMember members[] = {
        {
            .name = 82,
            .type = BUSTER_TYPE_INT,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
        },
    };
    check_test_expect(test, type_complete_record(table, record, members, 1, (TypeRecordAttributes){ 0 }), "plain record fixture construction");
    // A direct small-record access needs no scratch, independently of how many unrelated types the unit has.
    // The previous per-access frame/visited allocation fails this structural check even when its timing test passes.
    TypeId pointer_chain = record;
    for (uint32_t depth = 0; depth < 512 && pointer_chain != 0; depth += 1)
    {
        pointer_chain = type_pointer(table, pointer_chain);
    }
    Scratch member_scratch = scratch_begin(NULL);
    const uint64_t member_capacity = member_scratch.arena->capacity;
    member_scratch.arena->capacity = member_scratch.arena->length;
    bool allocation_free = pointer_chain != 0;
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        CheckActionResult lookup_result = { 0 };
        uint64_t offset = UINT64_MAX;
        uint32_t qualifiers = UINT32_MAX;
        const uint32_t member = check_action_find_member(table, record, 82, &offset, &qualifiers, scalar != 0, &lookup_result);
        allocation_free &= member != 0 && lookup_result.diagnostic == CHECK_DIAG_NONE && offset == 0 && qualifiers == 0;
    }
    member_scratch.arena->capacity = member_capacity;
    scratch_end(member_scratch);
    check_test_expect(test, allocation_free, "small-record action lookup needs no scratch after 512 unrelated pointer types on either path");
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = record,
                .category = CHECK_LVALUE
            },
            {
                .type = record,
                .category = CHECK_LVALUE
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == record, "compatible plain aggregate assignment accepted");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .type = BUSTER_TYPE_BOOL,
        .operands = {
            {
                .type = record,
                .category = CHECK_LVALUE
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic != CHECK_DIAG_NONE, "cast to bool still requires a scalar operand");
    const TypeId outer = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 83);
    const TypeMember outer_members[] = {
        {
            .name = 0,
            .type = record | BUSTER_TYPE_CONST,
            .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
        },
    };
    check_test_expect(test, type_complete_record(table, outer, outer_members, 1, (TypeRecordAttributes){ 0 }), "anonymous const record fixture construction");
    action = (CheckAction){
        .op = CHECK_OP_MEMBER,
        .member = 82,
        .operands = {
            {
                .type = outer,
                .category = CHECK_LVALUE
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == (BUSTER_TYPE_INT | BUSTER_TYPE_CONST),
                      "anonymous member path retains intermediate const qualification");
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            result.value,
            check_test_integer(BUSTER_TYPE_INT, 1)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "assignment through anonymous const record rejected");
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = outer,
                .category = CHECK_LVALUE
            },
            {
                .type = outer,
                .category = CHECK_LVALUE
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "aggregate with nested const member is not modifiable");

    action = (CheckAction){
        .op = CHECK_OP_ADDRESS,
        .operands = {
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = 11
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind != CHECK_CONSTANT_ADDRESS, "automatic address is not a static initializer constant");
    action.operands[0].flags = BUSTER_CHECK_STATIC_OBJECT;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 11, "static address retains relocation identity");
    action = (CheckAction){
        .op = CHECK_OP_ADD,
        .operands = {
            result.value,
            check_test_integer(BUSTER_TYPE_INT, 3)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 11 && result.value.offset == 12,
                      "static pointer arithmetic retains byte addend");
    const CheckValue address = result.value;
    action = (CheckAction){
        .op = CHECK_OP_SUBTRACT,
        .operands = {
            address,
            address
        }
    };
    action.operands[0].offset = 20;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 2 && result.value.type == BUSTER_TYPE_LONG,
                      "same-object pointer difference counts elements");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .type = pointer,
        .operands = {
            check_test_integer(BUSTER_TYPE_INT, UINT32_MAX)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 0 && result.value.offset == -1,
                      "integer-to-pointer cast sign extends to the target address width");
    const CheckValue absolute = result.value;
    action = (CheckAction){
        .op = CHECK_OP_NOT,
        .operands = {
            absolute
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 0,
                      "absolute nonzero address has true scalar value");
    action = (CheckAction){
        .op = CHECK_OP_SUBTRACT,
        .operands = {
            absolute,
            absolute
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_NONE,
                      "absolute pointers do not invent same-object provenance");
}

static __attribute__((noinline)) void check_test_integer_float_rounding(CheckTest* test, TypeTable* table)
{
    for (uint32_t format = 0; format < 2; format += 1)
    {
        const unsigned __int128 midpoint = ((unsigned __int128)1 << 100) + ((unsigned __int128)1 << (format == 0 ? 76 : 47));
        for (uint32_t side = 0; side < 3; side += 1)
        {
            const unsigned __int128 magnitude = midpoint - 1 + side;
            for (uint32_t sign = 0; sign < 3; sign += 1)
            {
                const __int128 number = sign == 1 ? -(__int128)magnitude : (__int128)magnitude;
                const TypeId source = sign == 2 ? BUSTER_TYPE_UINT128 : BUSTER_TYPE_INT128;
                const TypeId destination = format == 0 ? BUSTER_TYPE_FLOAT : BUSTER_TYPE_DOUBLE;
                const CheckAction action = {
                    .op = CHECK_OP_CAST,
                    .operands = {
                        check_test_integer(source, (unsigned __int128)number)
                    },
                    .type = destination,
                };
                const CheckActionResult result = check_test_action(test, table, action);
                bool equal;
                if (format == 0)
                {
                    const float rounded = side == 2 ? 0x1.000002p100f : 0x1p100f;
                    const float expected = sign == 1 ? -rounded : rounded;
                    const float actual = (float)result.value.floating;
                    uint32_t expected_bits;
                    uint32_t actual_bits;
                    memcpy(&expected_bits, &expected, sizeof(expected_bits));
                    memcpy(&actual_bits, &actual, sizeof(actual_bits));
                    equal = actual_bits == expected_bits;
                }
                else
                {
                    const double rounded = side == 2 ? 0x1.0000000000001p100 : 0x1p100;
                    const double expected = sign == 1 ? -rounded : rounded;
                    const double actual = (double)result.value.floating;
                    uint64_t expected_bits;
                    uint64_t actual_bits;
                    memcpy(&expected_bits, &expected, sizeof(expected_bits));
                    memcpy(&actual_bits, &actual, sizeof(actual_bits));
                    equal = actual_bits == expected_bits;
                }
                check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.fault == CHECK_FAULT_NONE &&
                                       result.value.constant_kind == CHECK_CONSTANT_FLOATING && equal,
                                  "128-bit integer converts directly to float/double without double rounding");
            }
        }
    }
}

static __attribute__((noinline)) void check_test_action_regressions(CheckTest* test, TypeTable* table)
{
    const CheckValue zero = check_test_integer(BUSTER_TYPE_INT, (unsigned __int128)1 << 32);
    CheckAction action = {
        .op = CHECK_OP_NOT,
        .operands = {
            zero
        }
    };
    CheckActionResult result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 1,
                      "logical truth ignores bits outside the integer's target width");
    action.op = CHECK_OP_CAST;
    action.type = BUSTER_TYPE_BOOL;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 0,
                      "bool conversion ignores bits outside the integer's target width");
    action.op = CHECK_OP_INITIALIZE;
    action.type = type_pointer(table, BUSTER_TYPE_INT);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 0 && result.value.offset == 0,
                      "null pointer conversion tests the target-width integer value");
    action = (CheckAction){
        .op = CHECK_OP_CONDITIONAL,
        .operands = {
            zero,
            check_test_integer(BUSTER_TYPE_INT, 3),
            check_test_integer(BUSTER_TYPE_INT, 7)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 7,
                      "conditional selection tests the target-width integer value");

    const TypeId parameter = BUSTER_TYPE_UINT;
    const TypeId old_style = type_function(table, BUSTER_TYPE_INT, NULL, 0, 0);
    const TypeId variadic = type_function(table, BUSTER_TYPE_INT, &parameter, 1, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC);
    const TypeId functions[] = {
        old_style,
        variadic
    };
    const CheckValue arguments[] = {
        check_test_integer(BUSTER_TYPE_INT, 0),
        {
            .type = BUSTER_TYPE_UINT,
            .category = CHECK_LVALUE,
            .flags = BUSTER_CHECK_BIT_FIELD,
            .bit_width = 3
        },
        {
            .type = BUSTER_TYPE_UINT,
            .category = CHECK_LVALUE,
            .flags = BUSTER_CHECK_BIT_FIELD,
            .bit_width = 32
        },
        check_test_float(BUSTER_TYPE_FLOAT, 1.25L)
    };
    CheckConversion conversions[BUSTER_ARRAY_LENGTH(arguments)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(functions); i += 1)
    {
        action = (CheckAction){
            .op = CHECK_OP_CALL,
            .operands = {
                {
                    .type = functions[i],
                    .category = CHECK_FUNCTION
                }
            },
            .arguments = arguments,
            .argument_count = BUSTER_ARRAY_LENGTH(arguments),
            .argument_conversions = conversions
        };
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && conversions[1].type == BUSTER_TYPE_INT &&
                               (conversions[1].flags & BUSTER_CHECK_CONVERT_PROMOTION) != 0 && conversions[2].type == BUSTER_TYPE_UINT &&
                               conversions[3].type == BUSTER_TYPE_DOUBLE && (conversions[3].flags & BUSTER_CHECK_CONVERT_PROMOTION) != 0,
                          "default arguments promote narrow bit-fields and float with explicit promotion metadata");
        check_test_expect(test, conversions[0].type == (i == 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT),
                          "fixed prototype arguments keep their parameter conversion");
    }
    const TypeId incomplete = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 101);
    const TypeId function = type_function(table, incomplete, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
    action = (CheckAction){
        .op = CHECK_OP_CALL,
        .operands = {
            {
                .type = function,
                .category = CHECK_FUNCTION
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INCOMPLETE, "a call cannot produce an incomplete record value");
    const TypeMember member = {
        .name = 102,
        .type = BUSTER_TYPE_INT,
        .bit_width = BUSTER_TYPE_NOT_BIT_FIELD
    };
    check_test_expect(test, type_complete_record(table, incomplete, &member, 1, (TypeRecordAttributes){ 0 }), "call return record completion");
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == incomplete, "a completed record return is callable");
    action.operands[0].type = type_function(table, BUSTER_TYPE_VOID, NULL, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_VOID, "a void return needs no object completeness");
}

static __attribute__((noinline)) void check_test_update_actions(CheckTest* test, TypeTable* table)
{
    const CheckOp compound[] = {
        CHECK_OP_ADD_ASSIGN,
        CHECK_OP_SUBTRACT_ASSIGN,
        CHECK_OP_MULTIPLY_ASSIGN,
        CHECK_OP_DIVIDE_ASSIGN,
        CHECK_OP_REMAINDER_ASSIGN,
        CHECK_OP_SHIFT_LEFT_ASSIGN,
        CHECK_OP_SHIFT_RIGHT_ASSIGN,
        CHECK_OP_BIT_AND_ASSIGN,
        CHECK_OP_BIT_OR_ASSIGN,
        CHECK_OP_BIT_XOR_ASSIGN
    };
    const uint32_t expected[] = {
        14,
        10,
        24,
        6,
        0,
        48,
        3,
        0,
        14,
        14
    };
    CheckAction action = { 0 };
    CheckActionResult result = { 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(compound); i += 1)
    {
        action = (CheckAction){
            .op = compound[i],
            .operands = {
                check_test_integer(BUSTER_TYPE_UCHAR, 12),
                check_test_integer(BUSTER_TYPE_INT, 2)
            }
        };
        action.operands[0].category = CHECK_LVALUE;
        result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == BUSTER_TYPE_UCHAR && result.value.integer == expected[i] &&
                               result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.category == CHECK_RVALUE &&
                               (result.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 && (result.value.flags & BUSTER_CHECK_ICE) == 0 &&
                               result.conversions[0].type == BUSTER_TYPE_INT && (result.conversions[0].flags & BUSTER_CHECK_CONVERT_LVALUE) != 0 &&
                               result.store_conversion.type == BUSTER_TYPE_UCHAR && result.store_conversion.flags == BUSTER_CHECK_CONVERT_ASSIGNMENT,
                          "compound assignment retains one converted LHS read and a separate narrowing store");
    }
    action.op = CHECK_OP_ADD_ASSIGN;
    action.operands[0] = check_test_integer(BUSTER_TYPE_UCHAR, 255);
    action.operands[0].category = CHECK_LVALUE;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.integer == 1 && result.value.fault == CHECK_FAULT_NONE, "compound assignment narrows after promoted arithmetic");
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 3);
    action.operands[0].category = CHECK_LVALUE;
    action.operands[1] = check_test_float(BUSTER_TYPE_DOUBLE, 0.75L);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 3 && result.conversions[0].type == BUSTER_TYPE_DOUBLE &&
                           result.store_conversion.type == BUSTER_TYPE_INT, "compound assignment computes in the common floating type before storing an integer");
    action.op = CHECK_OP_DIVIDE_ASSIGN;
    action.operands[1] = check_test_integer(BUSTER_TYPE_INT, 0);
    action.node = 111;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && result.value.fault_node == 111 &&
                           (result.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0, "compound division keeps its deferred evaluation fault");
    const CheckOp unary[] = {
        CHECK_OP_PRE_INCREMENT,
        CHECK_OP_PRE_DECREMENT,
        CHECK_OP_POST_INCREMENT,
        CHECK_OP_POST_DECREMENT
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unary); i += 1)
    {
        action = (CheckAction){
            .op = unary[i],
            .operands = {
                check_test_integer(BUSTER_TYPE_UCHAR, 255)
            }
        };
        action.operands[0].category = CHECK_LVALUE;
        result = check_test_action(test, table, action);
        const uint32_t value = i == 0 ? 0 : i == 1 ? 254 : 255;
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == value && result.value.type == BUSTER_TYPE_UCHAR &&
                               result.value.category == CHECK_RVALUE && (result.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                               (result.value.flags & BUSTER_CHECK_ICE) == 0 && result.conversions[1].type == 0 && result.conversions[1].flags == 0 &&
                               result.conversions[0].type == BUSTER_TYPE_INT && result.store_conversion.type == BUSTER_TYPE_UCHAR,
                          "prefix and postfix updates preserve their distinct values and implicit increment");
    }
    action.op = CHECK_OP_POST_INCREMENT;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, INT32_MAX);
    action.operands[0].category = CHECK_LVALUE;
    action.node = 112;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.fault == CHECK_FAULT_SIGNED_OVERFLOW && result.value.fault_node == 112 && result.value.constant_kind == CHECK_CONSTANT_NONE,
                      "postfix keeps overflow from its update even though its yielded value is the old value");
    action.op = CHECK_OP_PRE_DECREMENT;
    action.operands[0] = check_test_integer(BUSTER_TYPE_BOOL, 0);
    action.operands[0].category = CHECK_LVALUE;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 1, "bool decrement converts the promoted negative result back to bool");
    action.op = CHECK_OP_PRE_INCREMENT;
    action.operands[0] = check_test_integer(BUSTER_TYPE_UINT, 7);
    action.operands[0].category = CHECK_LVALUE;
    action.operands[0].flags |= BUSTER_CHECK_BIT_FIELD;
    action.operands[0].bit_width = 3;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 0 && result.value.bit_width == 3 &&
                           (result.value.flags & BUSTER_CHECK_BIT_FIELD) != 0, "prefix update narrows to the bit-field width and retains its promotion metadata");
    const CheckValue prefix = result.value;
    action.op = CHECK_OP_POST_INCREMENT;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 7 && (result.value.flags & BUSTER_CHECK_BIT_FIELD) == 0,
                      "postfix yields the ordinary value type rather than a bit-field promotion type");
    action = (CheckAction){
        .op = CHECK_OP_POSITIVE,
        .operands = {
            prefix
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.value.type == BUSTER_TYPE_INT, "prefix bit-field result receives its width-based integer promotion");
    action.op = CHECK_OP_SIZEOF;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == 4, "sizeof accepts a non-lvalue result of a bit-field update");
    action = (CheckAction){
        .op = CHECK_OP_ASSIGN,
        .operands = {
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .flags = BUSTER_CHECK_BIT_FIELD,
                .bit_width = 3
            },
            check_test_integer(BUSTER_TYPE_INT, 7)
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.integer == UINT32_MAX && (result.value.flags & BUSTER_CHECK_BIT_FIELD) != 0,
                      "signed bit-field assignment narrows and sign extends its result");

    action.op = CHECK_OP_PRE_INCREMENT;
    action.operands[0] = check_test_integer(BUSTER_TYPE_INT, 1);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_LVALUE_REQUIRED, "increment rejects an rvalue");
    action.operands[0].category = CHECK_LVALUE;
    action.operands[0].type |= BUSTER_TYPE_CONST;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "increment checks const before lvalue conversion");
    action.operands[0].type = type_array(table, BUSTER_TYPE_INT, 3);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED, "increment rejects arrays before decay");
    action.operands[0] = (CheckValue){
        .type = type_pointer(table, BUSTER_TYPE_VOID),
        .category = CHECK_LVALUE
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.type == action.operands[0].type,
                      "pointer update of a void * is GNU's byte arithmetic, which clang has outside GNU modes too");
    action.op = CHECK_OP_SUBTRACT_ASSIGN;
    action.operands[0].type = type_pointer(table, BUSTER_TYPE_INT);
    action.operands[1] = action.operands[0];
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_INTEGER_REQUIRED, "pointer compound subtraction requires an integer RHS");
}

static __attribute__((noinline)) void check_test_literals(CheckTest* test, Arena* scratch)
{
    const struct
    {
        const char* spelling;
        TypeId type;
        uint64_t integer;
        bool valid;
    } characters[] = {
        {
            .spelling = "'a'",
            .type = BUSTER_TYPE_INT,
            .integer = 'a',
            .valid = true
        },
        {
            .spelling = "'\\xff'",
            .type = BUSTER_TYPE_INT,
            .integer = UINT64_MAX,
            .valid = true
        },
        {
            .spelling = "'\\377'",
            .type = BUSTER_TYPE_INT,
            .integer = UINT64_MAX,
            .valid = true
        },
        {
            .spelling = "'ab'",
            .type = BUSTER_TYPE_INT,
            .integer = 0x6162,
            .valid = true
        },
        {
            .spelling = "'\\xff\\xff'",
            .type = BUSTER_TYPE_INT,
            .integer = 0xffff,
            .valid = true
        },
        {
            .spelling = "L'\\u20ac'",
            .type = BUSTER_TYPE_INT,
            .integer = 0x20ac,
            .valid = true
        },
        {
            .spelling = "u'\\u00e9'",
            .type = BUSTER_TYPE_USHORT,
            .integer = 0xe9,
            .valid = true
        },
        {
            .spelling = "U'\\U0001f600'",
            .type = BUSTER_TYPE_UINT,
            .integer = 0x1f600,
            .valid = true
        },
        {
            .spelling = "U'\\xffffffff'",
            .type = BUSTER_TYPE_UINT,
            .integer = UINT32_MAX,
            .valid = true
        },
        {
            .spelling = "u8'\\xff'",
            .type = BUSTER_TYPE_UCHAR,
            .integer = 255,
            .valid = true
        },
        {
            .spelling = "''"
        },
        {
            .spelling = "'''"
        },
        {
            .spelling = "'\n'"
        },
        {
            .spelling = "'\r'"
        },
        {
            .spelling = "'\\x'"
        },
        {
            .spelling = "'\\x100'"
        },
        {
            .spelling = "'\\400'"
        },
        {
            .spelling = "'\\uD800'"
        },
        {
            .spelling = "'\\U00110000'"
        },
        {
            .spelling = "'\\u123'"
        },
        {
            .spelling = "u'ab'"
        }
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(characters); i += 1)
    {
        const CheckLiteral value = check_literal_character((const uint8_t*)characters[i].spelling, (uint32_t)strlen(characters[i].spelling), false, false);
        check_test_expect(test, value.valid == characters[i].valid && (!value.valid || (value.type == characters[i].type && value.integer == characters[i].integer)),
                          "bounded character decoding matches the expected target value");
    }
    // Where plain char is unsigned (-funsigned-char), one ordinary character is its unsigned value, and the rest are as
    // they are where it is signed: multicharacter constants, u8's and the wide ones.
    static const struct
    {
        const char* spelling;
        uint64_t integer;
        // One ordinary character, whose value depends on char's signedness.
        bool single;
    } unsigned_characters[] = {
        { "'\\xff'", 255, true },
        { "'\\200'", 128, true },
        { "'\\x7f'", 127, true },
        { "'a'", 97, true },
        { "'\\xff\\xfe'", 0xfffe, false },
        { "u8'\\xff'", 255, false },
        { "L'\\xff'", 255, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsigned_characters); i += 1)
    {
        const char* spelling = unsigned_characters[i].spelling;
        const CheckLiteral value = check_literal_character((const uint8_t*)spelling, (uint32_t)strlen(spelling), false, true);
        const CheckLiteral signed_value = check_literal_character((const uint8_t*)spelling, (uint32_t)strlen(spelling), false, false);
        check_test_expect(test, value.valid && value.integer == unsigned_characters[i].integer &&
                                    (unsigned_characters[i].single || (signed_value.valid && signed_value.integer == value.integer)),
                          "a character constant has its unsigned char's value where plain char is unsigned");
    }
    const uint8_t embedded_zero[] = {
        '\'',
        '\\',
        0,
        '\''
    };
    check_test_expect(test, !check_literal_character(embedded_zero, sizeof(embedded_zero), false, false).valid, "an escaped NUL byte does not index past the escape table");
    check_test_expect(test, !check_literal_character(embedded_zero + 2, 0, false, false).valid, "an empty character spelling is invalid");
    const uint8_t basic_ucn[] = "'\\u0041'";
    const CheckLiteral old_ucn = check_literal_character(basic_ucn, sizeof(basic_ucn) - 1, false, false);
    const CheckLiteral new_ucn = check_literal_character(basic_ucn, sizeof(basic_ucn) - 1, true, false);
    check_test_expect(test, !old_ucn.valid && new_ucn.valid && new_ucn.integer == 65, "basic-character UCNs are accepted only from C23 onward");
    const uint64_t mark = scratch->length;
    CheckLiteral floating = { 0 };
    check_literal_number((const uint8_t*)"0x1.000002p100f", sizeof("0x1.000002p100f") - 1, scratch, &floating);
    check_test_expect(test, floating.valid && floating.type == BUSTER_TYPE_FLOAT && floating.floating == 0x1.000002p100L && scratch->length == mark,
                      "a hexadecimal float literal retains its source-format value and releases scratch");
    // _Float16's f16 suffix, with the values clang 23 gives (each checked there with a _Static_assert): midpoints of
    // _Float16 that the spelling is on, just above and just below (where strtod's double lands on the midpoint),
    // the subnormals and the overflow to infinity.
    const struct
    {
        const char* spelling;
        long double value;
    } halves[] =
    {
        { "1.5f16", 1.5L },
        { "0x1p3F16", 8.0L },
        { "0.1f16", 0x1.998p-4L },
        { "65504.0f16", 65504.0L },
        { "65519.0f16", 65504.0L },
        { "65520.0f16", __builtin_infl() },
        { "2049.0f16", 2048.0L },
        { "2049.0000000000000000001f16", 2050.0L },
        { "2048.9999999999999999999f16", 2048.0L },
        { "0x1p-24f16", 0x1p-24L },
        { "0x1p-25f16", 0.0L },
        { "0x1.8p-25f16", 0x1p-24L },
        { "0x1.ffcp15f16", 65504.0L },
        { "1e-8f16", 0.0L },
        { "3e-8f16", 0x1p-24L },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(halves); i += 1)
    {
        CheckLiteral half = { 0 };
        check_literal_number((const uint8_t*)halves[i].spelling, (uint32_t)strlen(halves[i].spelling), scratch, &half);
        char description[128];
        snprintf(description, sizeof(description), "%s is a _Float16 constant with clang's value", halves[i].spelling);
        check_test_expect(test, half.valid && half.type == BUSTER_TYPE_FLOAT16 && half.floating == halves[i].value && scratch->length == mark, description);
    }
    const char* imaginary_halves[] =
    {
        "1.5f16i",
        "1.5if16",
        "1.5F16j",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(imaginary_halves); i += 1)
    {
        CheckLiteral half = { 0 };
        check_literal_number((const uint8_t*)imaginary_halves[i], (uint32_t)strlen(imaginary_halves[i]), scratch, &half);
        check_test_expect(test, half.valid && half.imaginary && half.type == BUSTER_TYPE_COMPLEX(type_index(BUSTER_TYPE_FLOAT16)) && half.floating == 1.5L,
                          "an imaginary f16 literal, with its i before or after the f16");
    }
    const char* invalid_halves[] =
    {
        "1.5f16f16",
        "1.5lf16",
        "1.5f15",
        "1.5f1",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_halves); i += 1)
    {
        CheckLiteral half = { 0 };
        check_literal_number((const uint8_t*)invalid_halves[i], (uint32_t)strlen(invalid_halves[i]), scratch, &half);
        check_test_expect(test, !half.valid && !half.resource, "a suffix that only starts like f16 is invalid, as clang has it");
    }
    CheckLiteral hexadecimal = { 0 };
    check_literal_number((const uint8_t*)"0x1f16", sizeof("0x1f16") - 1, scratch, &hexadecimal);
    check_test_expect(test, hexadecimal.valid && hexadecimal.type == BUSTER_TYPE_INT && hexadecimal.integer == 0x1f16, "a hexadecimal integer ending in f16 is an integer");
    // Bit-precise constants (#768): C23's wb and WB, alone or with u or U before or after them, in each radix and with
    // digit separators, as the widths and values clang 23 gives them (each checked there by _Generic), a width past 128
    // bits and a value past them (UINT32_MAX), which the reader refuses by name; and the spellings C23 doesn't have,
    // with the imaginary one, which clang takes as a _Complex _BitInt it doesn't let a program spell and GCC refuses.
    const struct
    {
        const char* spelling;
        uint32_t width;
        bool is_unsigned;
        uint64_t high;
        uint64_t low;
    } bit_precise[] =
    {
        { "0wb", 2, false, 0, 0 },
        { "1wb", 2, false, 0, 1 },
        { "2WB", 3, false, 0, 2 },
        { "0uwb", 1, true, 0, 0 },
        { "1Uwb", 1, true, 0, 1 },
        { "5uWB", 3, true, 0, 5 },
        { "5wbu", 3, true, 0, 5 },
        { "5WBU", 3, true, 0, 5 },
        { "0777wb", 10, false, 0, 511 },
        { "0b1010'1010wb", 9, false, 0, 170 },
        { "0x7fff'ffff'ffff'ffffwb", 64, false, 0, INT64_MAX },
        { "0x8000000000000000wb", 65, false, 0, UINT64_C(0x8000000000000000) },
        { "0xffffffffffffffffuwb", 64, true, 0, UINT64_MAX },
        { "18446744073709551616wb", 66, false, 1, 0 },
        { "0x1'0000'0000'0000'0000uwb", 65, true, 1, 0 },
        { "0x7fffffffffffffffffffffffffffffffwb", 128, false, INT64_MAX, UINT64_MAX },
        { "0xffffffffffffffffffffffffffffffffuwb", 128, true, UINT64_MAX, UINT64_MAX },
        { "0xffffffffffffffffffffffffffffffffwb", 129, false, UINT64_MAX, UINT64_MAX },
        { "340282366920938463463374607431768211456uwb", UINT32_MAX, true, 0, 0 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bit_precise); i += 1)
    {
        CheckLiteral literal = { 0 };
        check_literal_number((const uint8_t*)bit_precise[i].spelling, (uint32_t)strlen(bit_precise[i].spelling), scratch, &literal);
        const unsigned __int128 value = (unsigned __int128)bit_precise[i].high << 64 | bit_precise[i].low;
        char description[160];
        snprintf(description, sizeof(description), "%s is a bit-precise constant of %u bits with clang's value", bit_precise[i].spelling, bit_precise[i].width);
        check_test_expect(test, literal.valid && literal.type == 0 && literal.bit_width == bit_precise[i].width && literal.bit_unsigned == bit_precise[i].is_unsigned &&
                                (bit_precise[i].width == UINT32_MAX || ((unsigned __int128)literal.high << 64 | literal.integer) == value) && !literal.imaginary &&
                                scratch->length == mark,
                          description);
    }
    const char* invalid_bit_precise[] =
    {
        "5wB",
        "5Wb",
        "5uwbu",
        "5UwbU",
        "5wbl",
        "5lwb",
        "5llwb",
        "5wbll",
        "5wbwb",
        "5wbi",
        "5iwb",
        "5w",
        "5b",
        "5uw",
        "0b2wb",
        "09wb",
        "5'wb",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_bit_precise); i += 1)
    {
        CheckLiteral literal = { 0 };
        check_literal_number((const uint8_t*)invalid_bit_precise[i], (uint32_t)strlen(invalid_bit_precise[i]), scratch, &literal);
        char description[128];
        snprintf(description, sizeof(description), "%s is no constant C23 has, as clang has it", invalid_bit_precise[i]);
        check_test_expect(test, !literal.valid && !literal.resource && scratch->length == mark, description);
    }
}

static __attribute__((noinline)) void check_test_action_oracles(CheckTest* test, const char* clang)
{
    // These language rules need record syntax or GNU folded floating assertions that the source adapter does not
    // yet implement. Clang checks original source independently of the direct action and literal fixtures above.
    const char* sources[] = {
        "struct B { unsigned int narrow : 3; unsigned int full : 32; };\n"
        "_Static_assert(_Generic(+((struct B *)0)->narrow, int: 1, default: 0), \"narrow bit-field promotion\");\n"
        "_Static_assert(_Generic(+((struct B *)0)->full, unsigned int: 1, default: 0), \"full-width bit-field promotion\");\n",
        "struct S; struct S f(void);\nvoid g(void) { f(); }\n",
        "struct S { int x; }; struct S f(void);\nvoid g(void) { f(); }\n",
        "_Static_assert(!(int)4294967296ull && (_Bool)(int)4294967296ull == 0, \"integer truth width\");\n"
        "int *p = (int)4294967296ull;\n",
        "_Static_assert('\\xff' == -1 && '\\377' == -1 && 'ab' == 0x6162 && '\\xff\\xff' == 65535, \"ordinary characters\");\n"
        "_Static_assert(L'\\u20ac' == 0x20ac && u'\\u00e9' == 0xe9 && U'\\U0001f600' == 0x1f600, \"unicode characters\");\n"
        "_Static_assert(U'\\xffffffff' == 0xffffffffu, \"wide numeric escape\");\n",
        "struct B { unsigned int narrow : 3; } b;\n"
        "_Static_assert(_Generic(+(b.narrow = 0), int: 1, default: 0), \"assignment bit-field promotion\");\n"
        "_Static_assert(_Generic(+(b.narrow += 0), int: 1, default: 0), \"compound bit-field promotion\");\n"
        "_Static_assert(_Generic(+(++b.narrow), int: 1, default: 0), \"prefix bit-field promotion\");\n"
        "_Static_assert(_Generic(+(b.narrow++), unsigned int: 1, default: 0), \"postfix ordinary value\");\n"
        "_Static_assert(sizeof(b.narrow = 0) == 4 && sizeof(++b.narrow) == 4 && sizeof(b.narrow++) == 4, \"non-lvalue sizeof\");\n",
        "_Static_assert('\\u0041' == 65, \"basic UCN language mode\");\n"
    };
    enum
    {
        ROUNDING_COUNT = 36,
        FILE_COUNT = BUSTER_ARRAY_LENGTH(sources) + ROUNDING_COUNT
    };
    char directory[] = "/tmp/buster-checker-actions-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    bool rejected[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        char generated[512];
        const char* source;
        if (i < BUSTER_ARRAY_LENGTH(sources))
        {
            source = sources[i];
            rejected[i] = i == 1 || i + 1 == BUSTER_ARRAY_LENGTH(sources);
        }
        else
        {
            const uint32_t r = i - BUSTER_ARRAY_LENGTH(sources);
            const uint32_t form = r / 2;
            const uint32_t format = form / 9;
            const uint32_t side = (form / 3) % 3;
            const uint32_t sign = form % 3;
            const bool mutate = (r & 1) != 0;
            const bool upper = (side == 2) != mutate;
            const char* expected = format == 0 ? (upper ? "0x1.000002p100f" : "0x1p100f") : (upper ? "0x1.0000000000001p100" : "0x1p100");
            snprintf(generated, sizeof(generated),
                     "_Static_assert((%s)(%s(%s)((((unsigned __int128)1 << 100) + ((unsigned __int128)1 << %u)) - 1 + %u)) == %s%s, \"integer conversion midpoint\");\n",
                     format == 0 ? "float" : "double", sign == 1 ? "-" : "", sign == 2 ? "unsigned __int128" : "__int128",
                     format == 0 ? 76 : 47, side, sign == 1 ? "-" : "", expected);
            source = generated;
            rejected[i] = mutate;
        }
        ready = parse_test_write(paths[i], source, strlen(source));
    }
    const char* arguments[] = {
        "-std=gnu17"
    };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    check_test_expect(test, ready, "independent Clang action oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == rejected[i], "independent Clang action expectation or its mutation differs");
    }
    if (ready)
    {
        const char* c23_arguments[] = {
            "-std=gnu23"
        };
        ParseTestClang c23;
        const bool checked = parse_test_clang(clang, c23_arguments, BUSTER_ARRAY_LENGTH(c23_arguments), pointers + BUSTER_ARRAY_LENGTH(sources) - 1, 1, &c23);
        check_test_expect(test, checked && !c23.rejected, "Clang accepts a basic-character UCN in C23");
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_actions_batch(CheckTest* test, TypeTable* table)
{
    CheckAction actions[BUSTER_CHECK_TEST_BATCH];
    CheckActionResult expected[BUSTER_CHECK_TEST_BATCH];
    CheckActionResult actual[BUSTER_CHECK_TEST_BATCH];
    const TypeId types[] = {
        BUSTER_TYPE_SCHAR,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG,
        BUSTER_TYPE_LLONG,
        BUSTER_TYPE_ULLONG,
        BUSTER_TYPE_INT128,
        BUSTER_TYPE_UINT128,
    };
    const CheckOp operations[] = {
        CHECK_OP_ADD,
        CHECK_OP_SUBTRACT,
        CHECK_OP_MULTIPLY,
        CHECK_OP_DIVIDE,
        CHECK_OP_REMAINDER,
        CHECK_OP_SHIFT_LEFT,
        CHECK_OP_SHIFT_RIGHT,
        CHECK_OP_LESS,
        CHECK_OP_GREATER_EQUAL,
        CHECK_OP_EQUAL,
        CHECK_OP_NOT_EQUAL,
        CHECK_OP_BIT_AND,
        CHECK_OP_BIT_OR,
        CHECK_OP_BIT_XOR,
        CHECK_OP_LOGICAL_AND,
        CHECK_OP_LOGICAL_OR,
    };
    check_actions(table, 0, 0, 0);
    for (uint32_t world = 0; world < 128; world += 1)
    {
        for (uint32_t i = 0; i < BUSTER_CHECK_TEST_BATCH; i += 1)
        {
            const uint32_t a = check_test_random(test);
            const uint32_t b = check_test_random(test);
            const TypeId at = types[check_test_random(test) % BUSTER_ARRAY_LENGTH(types)];
            const TypeId bt = types[check_test_random(test) % BUSTER_ARRAY_LENGTH(types)];
            actions[i] = (CheckAction){
                .op = operations[check_test_random(test) % BUSTER_ARRAY_LENGTH(operations)],
                .operands = {
                    check_test_integer(at, (uint64_t)a << 32 | b),
                    check_test_integer(bt, i % 7 == 0 ? 0 : b),
                },
                .node = i + 1000,
            };
            if (i % 17 == 0)
            {
                actions[i].operands[0] = check_test_float(BUSTER_TYPE_DOUBLE, (int32_t)a / 16.0L);
            }
            check_action_scalar(table, &actions[i], &expected[i]);
        }
        const uint32_t count = world < 65 ? world : BUSTER_CHECK_TEST_BATCH - (world % 17);
        memset(actual, 0x5a, sizeof(actual));
        check_actions(table, actions, actual, count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_test_expect(test, check_test_action_equal(&expected[i], &actual[i]), "random action batch differs from scalar");
        }
        for (uint32_t i = 0; i < count;)
        {
            const uint32_t remaining = count - i;
            const uint32_t length = 1 + check_test_random(test) % (remaining < 19 ? remaining : 19);
            check_actions(table, actions + i, actual + i, length);
            i += length;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            check_test_expect(test, check_test_action_equal(&expected[i], &actual[i]), "action output depends on legal batch partition");
        }
    }

    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t input_bytes = (uint64_t)sizeof(CheckAction) * 33;
    const uint64_t output_bytes = (uint64_t)sizeof(CheckActionResult) * 33;
    const uint64_t input_pages = (input_bytes + page - 1) / page * page;
    const uint64_t output_pages = (output_bytes + page - 1) / page * page;
    uint8_t* input_mapping = mmap(0, input_pages + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* output_mapping = mmap(0, output_pages + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = input_mapping != MAP_FAILED && output_mapping != MAP_FAILED &&
                         mprotect(input_mapping + input_pages, page, PROT_NONE) == 0 && mprotect(output_mapping + output_pages, page, PROT_NONE) == 0;
    check_test_expect(test, guarded, "guard-page allocation");
    if (guarded)
    {
        for (uint32_t count = 0; count <= 33; count += 1)
        {
            CheckAction* input = (CheckAction*)(input_mapping + input_pages - (uint64_t)count * sizeof(CheckAction));
            CheckActionResult* output = (CheckActionResult*)(output_mapping + output_pages - (uint64_t)count * sizeof(CheckActionResult));
            if (count != 0)
            {
                memcpy(input, actions, (uint64_t)count * sizeof(CheckAction));
            }
            check_actions(table, input, output, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_test_expect(test, check_test_action_equal(&expected[i], &output[i]), "guarded action tail differs");
            }
        }
    }
    if (input_mapping != MAP_FAILED)
    {
        munmap(input_mapping, input_pages + page);
    }
    if (output_mapping != MAP_FAILED)
    {
        munmap(output_mapping, output_pages + page);
    }
}

static bool check_test_stream_equal(const CheckStream* a, const CheckStream* b, uint32_t arguments)
{
    bool equal = a->status == b->status && a->count == b->count && a->report_count == b->report_count && a->effect_count == b->effect_count &&
                 a->varargs_count == b->varargs_count && check_builtin_stream_metadata_equal(a, b, check_test_value_equal);
    for (uint32_t i = 0; equal && i < a->count; i += 1)
    {
        equal = check_test_value_equal(&a->values[i], &b->values[i]) && a->diagnostics[i] == b->diagnostics[i] && a->warnings[i] == b->warnings[i] &&
                (a->diagnostic_operands != NULL ? a->diagnostic_operands[i] : 0) == (b->diagnostic_operands != NULL ? b->diagnostic_operands[i] : 0) &&
                (a->member_offsets != NULL ? a->member_offsets[i] : 0) == (b->member_offsets != NULL ? b->member_offsets[i] : 0) &&
                (a->warning_ignored != NULL ? a->warning_ignored[i] : 0) == (b->warning_ignored != NULL ? b->warning_ignored[i] : 0) &&
                a->conversions[i].store.type == b->conversions[i].store.type && a->conversions[i].store.flags == b->conversions[i].store.flags;
        for (uint32_t operand = 0; equal && operand < 3; operand += 1)
        {
            equal = a->conversions[i].operands[operand].type == b->conversions[i].operands[operand].type &&
                    a->conversions[i].operands[operand].flags == b->conversions[i].operands[operand].flags;
        }
    }
    for (uint32_t i = 0; equal && i < arguments; i += 1)
    {
        equal = a->argument_conversions[i].type == b->argument_conversions[i].type &&
                a->argument_conversions[i].flags == b->argument_conversions[i].flags;
    }
    for (uint32_t i = 0; equal && i < a->report_count; i += 1)
    {
        const CheckReport* x = &a->reports[i];
        const CheckReport* y = &b->reports[i];
        equal = x->node == y->node && x->token == y->token && x->message == y->message && x->diagnostic == y->diagnostic && x->fault == y->fault;
    }
    for (uint32_t i = 0; equal && i < a->effect_count; i += 1)
    {
        equal = check_store_effect_equal(a->effects + i, b->effects + i, check_test_value_equal);
    }
    for (uint32_t i = 0; equal && i < a->varargs_count; i += 1)
    {
        const CheckVarargsEffect* x = a->varargs + i;
        const CheckVarargsEffect* y = b->varargs + i;
        equal = x->node == y->node && x->requested_type == y->requested_type && x->evaluated_operands == y->evaluated_operands &&
                x->warnings == y->warnings && x->mutates_list == y->mutates_list;
    }
    return equal;
}

// `graph` cut to its first `count` nodes, with the leaves of the VALUE nodes among them.
static __attribute__((unused)) void check_test_graph_cut(CheckGraph* graph, uint32_t count)
{
    graph->count = count;
    graph->leaf_count = 0;
    for (uint32_t node = 0; node < count; node += 1)
    {
        graph->leaf_count += graph->ops[node] == CHECK_OP_VALUE;
    }
}

static uint32_t check_test_leaf(CheckGraph* graph, uint32_t token, CheckValue value)
{
    const uint32_t node = check_graph_add(graph, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    if (node != BUSTER_CHECK_NONE)
    {
        *check_graph_leaf_of(graph, node) = value;
    }
    return node;
}

static void check_test_compare_graph(CheckTest* test, TypeTable* table, const CheckGraph* graph, Arena* output, Arena* scratch, bool partitions)
{
    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const CheckOptions scalar_options = {
        .scalar = true
    };
    CheckStream scalar;
    const bool scalar_valid = check_graph_run(table, graph, output, scratch, &scalar_options, &scalar);
    check_test_expect(test, scalar_valid && scalar.status == CHECK_STATUS_OK && scalar.count == graph->count, "scalar graph run failed");
    check_test_expect(test, scratch->length == scratch_mark, "scalar graph retained scratch storage");
    const uint64_t scalar_end = output->length;
    CheckGraph recorded = *graph;
    recorded.recorded = (CheckGraphValidation){ 0 };
    recorded.recorded_nodes = 0;
    recorded.recorded_ready = false;
    recorded.recorded_pure = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : NULL;
    const bool recorded_room = graph->count == 0 || recorded.recorded_pure != NULL;
    check_test_expect(test, recorded_room, "recorded graph oracle allocation");
    if (scalar_valid && recorded_room)
    {
        check_graph_record_finish(&recorded);
        for (uint32_t path = 0; path < 6; path += 1)
        {
            const CheckOptions options = { .scalar = path % 3 == 0, .verify = path >= 3, .level_order = path % 3 == 2 };
            CheckStream stream;
            const uint64_t recorded_mark = scratch->length;
            const bool checked = check_graph_run(table, &recorded, output, scratch, &options, &stream);
            check_test_expect(test, checked && check_test_stream_equal(&scalar, &stream, graph->argument_count),
                              "recorded graph facts agree with both validators and all three schedules");
            check_test_expect(test, scratch->length == recorded_mark, "recorded graph run releases only its own scratch");
            arena_pop_to(output, scalar_end);
        }
    }
    arena_pop_to(scratch, scratch_mark);
    // Each batch size by both of the vector path's orders, in postorder and by level (CheckOptions.level_order).
    for (uint32_t run = partitions ? 2 : 2 * BUSTER_CHECK_BATCH; scalar_valid && run < 2 * (BUSTER_CHECK_BATCH + 1); run += 1)
    {
        const uint32_t batch = run / 2;
        const CheckOptions vector_options = {
            .batch_size = batch,
            .level_order = (run & 1) != 0,
        };
        CheckStream vector;
        const bool vector_valid = check_graph_run(table, graph, output, scratch, &vector_options, &vector);
        check_test_expect(test, vector_valid && check_test_stream_equal(&scalar, &vector, graph->argument_count),
                          (run & 1) != 0 ? "graph paths or level-order batch partitions differ" : "graph paths or batch partitions differ");
        check_test_expect(test, scratch->length == scratch_mark, "vector graph retained scratch storage");
        if (vector_valid)
        {
            check_test_expect(test, vector.statistics.nodes == graph->count, "graph statistics lost nodes");
            for (uint32_t i = 1; i < vector.report_count; i += 1)
            {
                check_test_expect(test, vector.reports[i - 1].token <= vector.reports[i].token, "diagnostics are not in source-token order");
            }
        }
        arena_pop_to(output, scalar_end);
    }
    arena_pop_to(output, mark);
}

static __attribute__((noinline)) void check_test_graph_fixtures(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    CheckGraph graph;
    const bool initialized = check_graph_initialize(&graph, storage, 2048, 32);
    check_test_expect(test, initialized, "graph fixture allocation");
    if (initialized)
    {
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const uint32_t one = check_test_leaf(&graph, 0, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t zero = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, 0));
        const uint32_t divide = check_graph_add(&graph, CHECK_OP_DIVIDE, 2, one, zero, BUSTER_CHECK_NONE);
        const uint32_t logical = check_graph_add(&graph, CHECK_OP_LOGICAL_AND, 3, zero, divide, BUSTER_CHECK_NONE);
        graph.contexts[logical] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t size = check_graph_add(&graph, CHECK_OP_SIZEOF, 4, divide, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.contexts[size] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t selected = check_graph_add(&graph, CHECK_OP_LOGICAL_OR, 8, zero, divide, BUSTER_CHECK_NONE);
        graph.contexts[selected] = BUSTER_CHECK_CONTEXT_ICE;
        const uint32_t assertion = check_graph_add(&graph, CHECK_OP_VALUE, 7, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        *check_graph_leaf_of(&graph, assertion) = check_test_integer(BUSTER_TYPE_INT, 0);
        graph.contexts[assertion] = BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_ICE;
        // Both children are checked even when evaluation selects only one: invalid operand types remain errors.
        const uint32_t floating = check_test_leaf(&graph, 9, check_test_float(BUSTER_TYPE_FLOAT, 1));
        const uint32_t invalid = check_graph_add(&graph, CHECK_OP_REMAINDER, 10, floating, one, BUSTER_CHECK_NONE);
        check_graph_add(&graph, CHECK_OP_LOGICAL_AND, 11, zero, invalid, BUSTER_CHECK_NONE);
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        CheckStream stream;
        const CheckOptions options = { 0 };
        const uint64_t output_mark = output->length;
        const bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, valid, "context fixture checker run");
        if (valid)
        {
            check_test_expect(test, stream.values[logical].fault == CHECK_FAULT_NONE && stream.values[logical].integer == 0, "graph short circuit evaluated an unselected operand");
            check_test_expect(test, stream.values[size].integer == 4 && stream.values[size].fault == CHECK_FAULT_NONE, "graph sizeof evaluated its operand");
            check_test_expect(test, stream.values[selected].fault == CHECK_FAULT_DIVIDE_BY_ZERO, "graph lost selected evaluation fault");
            check_test_expect(test, stream.diagnostics[invalid] == CHECK_DIAG_INTEGER_REQUIRED, "graph hid a constraint violation in an unevaluated branch");
            bool assertion_report = false;
            bool evaluation_report = false;
            for (uint32_t i = 0; i < stream.report_count; i += 1)
            {
                assertion_report |= stream.reports[i].node == assertion && stream.reports[i].message == CHECK_MESSAGE_STATIC_ASSERT;
                evaluation_report |= stream.reports[i].message == CHECK_MESSAGE_CONSTANT && stream.reports[i].fault == CHECK_FAULT_DIVIDE_BY_ZERO;
            }
            check_test_expect(test, assertion_report, "false static assertion has no diagnostic");
            check_test_expect(test, evaluation_report, "required constant evaluation fault has no diagnostic");
        }
        arena_pop_to(output, output_mark);
        graph.count = 0;
        graph.leaf_count = 0;
        // All expressions would have the same bracket depth in `a+b+c`. Edges, not parser depths, carry readiness.
        uint32_t previous = check_test_leaf(&graph, 0, check_test_integer(BUSTER_TYPE_UINT, 0));
        for (uint32_t i = 1; i <= 127; i += 1)
        {
            const uint32_t value = check_test_leaf(&graph, i * 2 - 1, check_test_integer(BUSTER_TYPE_UINT, 1));
            previous = check_graph_add(&graph, CHECK_OP_ADD, i * 2, previous, value, BUSTER_CHECK_NONE);
        }
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const bool chain_valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, chain_valid && stream.values[previous].integer == 127, "same-bracket-depth dependency chain was evaluated too early");
        arena_pop_to(output, output_mark);
        graph.count = 0;
        graph.leaf_count = 0;
        for (uint32_t i = 0; i < 257; i += 1)
        {
            const uint32_t a = check_test_leaf(&graph, i * 3, check_test_integer(BUSTER_TYPE_ULONG, i));
            const uint32_t b = check_test_leaf(&graph, i * 3 + 1, check_test_integer(BUSTER_TYPE_ULONG, i + 1));
            check_graph_add(&graph, CHECK_OP_MULTIPLY, i * 3 + 2, a, b, BUSTER_CHECK_NONE);
        }
        check_test_compare_graph(test, table, &graph, output, scratch, true);
    }
    arena_pop_to(storage, mark);
}

static __attribute__((noinline)) void check_test_graph_random(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    CheckGraph graph;
    const bool initialized = check_graph_initialize(&graph, storage, 8193, 0);
    check_test_expect(test, initialized, "random graph allocation");
    const CheckOp operations[] = {
        CHECK_OP_ADD,
        CHECK_OP_SUBTRACT,
        CHECK_OP_MULTIPLY,
        CHECK_OP_BIT_XOR,
        CHECK_OP_BIT_AND,
        CHECK_OP_BIT_OR,
        CHECK_OP_LESS,
        CHECK_OP_NOT_EQUAL,
        CHECK_OP_LOGICAL_AND,
        CHECK_OP_LOGICAL_OR,
    };
    for (uint32_t world = 0; initialized && world < 12; world += 1)
    {
        graph.count = 0;
        graph.leaf_count = 0;
        for (uint32_t i = 0; i < graph.capacity; i += 1)
        {
            if (i < 2 || (check_test_random(test) & 7) == 0)
            {
                check_test_leaf(&graph, i, check_test_integer(BUSTER_TYPE_ULONG, check_test_random(test)));
            }
            else
            {
                const uint32_t a = check_test_random(test) % i;
                const uint32_t b = check_test_random(test) % i;
                const CheckOp op = operations[check_test_random(test) % BUSTER_ARRAY_LENGTH(operations)];
                check_graph_add(&graph, op, i, a, b, BUSTER_CHECK_NONE);
            }
        }
        check_test_compare_graph(test, table, &graph, output, scratch, world < 2);
    }
    arena_pop_to(storage, mark);
}

static void check_test_graph_recorded(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    CheckGraph graph;
    bool ready = check_graph_initialize(&graph, storage, 33, 0);
    graph.recorded_pure = ready ? BUSTER_ARENA_PUSH_ARRAY(storage, uint8_t, graph.capacity) : NULL;
    ready = ready && graph.recorded_pure != NULL;
    check_test_expect(test, ready, "recorded graph mutation fixture allocation");
    if (ready)
    {
        uint32_t previous = BUSTER_CHECK_NONE;
        for (uint32_t node = 0; node < 32; node += 1)
        {
            // Match the source reader's protocol: finish the preceding node only after its caller filled it.
            check_graph_record_nodes(&graph);
            previous = node == 0 ? check_test_leaf(&graph, node, check_test_integer(BUSTER_TYPE_INT, 7)) :
                       check_graph_add(&graph, CHECK_OP_POSITIVE, node, previous, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        check_graph_record_finish(&graph);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const CheckOptions options = { .scalar = scalar != 0, .verify = true };
            CheckStream stream;
            bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, checked, "incremental recorded graph facts verify across a vector block boundary");
            arena_pop_to(output, output_mark);
            const CheckGraphValidation original = graph.recorded;
            for (uint32_t byte = 0; byte < offsetof(CheckGraphValidation, vector_blocks); byte += sizeof(uint32_t))
            {
                ((uint8_t*)&graph.recorded)[byte] ^= 1;
                checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
                check_test_expect(test, !checked && stream.status == CHECK_STATUS_INVALID_GRAPH &&
                                  output->length == output_mark && scratch->length == scratch_mark,
                                  "verification rejects every stale allocation counter without publishing output");
                graph.recorded = original;
            }
            for (uint32_t node = 0; node < graph.count; node += 1)
            {
                graph.recorded_pure[node] ^= 1;
                checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
                check_test_expect(test, !checked && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == node &&
                                  output->length == output_mark && scratch->length == scratch_mark,
                                  "verification identifies each stale purity byte and rolls back");
                graph.recorded_pure[node] ^= 1;
            }
            graph.operand0[31] = graph.count;
            checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
            check_test_expect(test, !checked && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == 31,
                              "verification still checks the reader graph's operand topology");
            graph.operand0[31] = 30;
        }
        check_test_leaf(&graph, graph.count, check_test_integer(BUSTER_TYPE_INT, 8));
        check_test_expect(test, !graph.recorded_ready, "appending a node invalidates a previously published summary");
        graph.operand0[31] = graph.count;
        const CheckOptions options = { 0 };
        CheckStream stream;
        const bool checked = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !checked && stream.status == CHECK_STATUS_INVALID_GRAPH && stream.failed_node == 31,
                          "an invalidated summary uses the ordinary validator without the verify option");
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(storage, mark);
}

static __attribute__((noinline)) void check_test_graph_limits(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    CheckGraph graph;
    const bool initialized = check_graph_initialize(&graph, storage, 65537, 0);
    check_test_expect(test, initialized, "deep graph allocation");
    if (initialized)
    {
        uint32_t previous = check_test_leaf(&graph, 0, check_test_integer(BUSTER_TYPE_UINT, 7));
        for (uint32_t i = 1; i < graph.capacity; i += 1)
        {
            previous = check_graph_add(&graph, CHECK_OP_POSITIVE, i, previous, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        }
        check_test_expect(test, previous == graph.capacity - 1, "deep graph construction failed");
        check_test_compare_graph(test, table, &graph, output, scratch, false);
        const uint32_t full_count = graph.count;
        const uint32_t over = check_graph_add(&graph, CHECK_OP_POSITIVE, graph.count, previous, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        check_test_expect(test, over == BUSTER_CHECK_NONE && graph.count == full_count, "full graph append did not fail atomically");
        check_test_graph_cut(&graph, 2);
        CheckStream stream;
        const CheckOptions options = { 0 };
        const uint64_t output_mark = output->length;
        const uint64_t scratch_mark = scratch->length;
        graph.operand0[1] = 1;
        bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH && output->length == output_mark && scratch->length == scratch_mark,
                          "self-dependent graph was not rejected atomically");
        graph.operand0[1] = 65535;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "out-of-range dependency was not rejected");
        graph.operand0[1] = 0;
        graph.ops[1] = CHECK_OP_COUNT;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "invalid operation was not rejected");
        graph.ops[1] = CHECK_OP_POSITIVE;
        graph.leaf_slots[1] = BUSTER_CHECK_NONE;
        graph.leaf_count = 1;
        graph.operand0[1] = BUSTER_CHECK_NONE;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "missing operand was not rejected");
        graph.operand0[1] = 0;
        Arena no_room = { 0 };
        valid = check_graph_run(table, &graph, &no_room, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_RESOURCE && no_room.length == 0 && scratch->length == scratch_mark,
                          "exhausted output did not fail atomically");
        valid = check_graph_run(table, &graph, output, &no_room, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_RESOURCE && output->length == output_mark && no_room.length == 0,
                          "exhausted scratch did not fail atomically");
        const CheckOptions invalid_options = {
            .batch_size = BUSTER_CHECK_BATCH + 1
        };
        valid = check_graph_run(table, &graph, output, scratch, &invalid_options, &stream);
        check_test_expect(test, !valid, "invalid batch size was silently accepted");
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    arena_pop_to(storage, mark);
}

static __attribute__((noinline)) void check_test_graph_type_order(CheckTest* test, Arena* storage, Arena* output, Arena* scratch)
{
    CheckTestTypes types[3] = { 0 };
    const bool initialized = check_test_types_open(&types[0]) && check_test_types_open(&types[1]) && check_test_types_open(&types[2]);
    check_test_expect(test, initialized, "independent type tables for scheduling-order test");
    const uint64_t mark = storage->length;
    const uint64_t output_mark = output->length;
    if (initialized)
    {
        TypeId array_int[3];
        TypeId pointer_array_int[3];
        TypeId array_short[3];
        for (uint32_t path = 0; path < 3; path += 1)
        {
            array_int[path] = type_array(&types[path].table, BUSTER_TYPE_INT, 3);
            pointer_array_int[path] = type_pointer(&types[path].table, array_int[path]);
            array_short[path] = type_array(&types[path].table, BUSTER_TYPE_SHORT, 5);
        }
        check_test_expect(test, array_int[0] == array_int[1] && pointer_array_int[0] == pointer_array_int[1] && array_short[0] == array_short[1],
                          "type-order fixture input identities");
        check_test_expect(test, array_int[0] == array_int[2] && pointer_array_int[0] == pointer_array_int[2] && array_short[0] == array_short[2],
                          "level-order fixture input identities");
        CheckGraph graph;
        const bool allocated = check_graph_initialize(&graph, storage, 8, 0);
        check_test_expect(test, allocated, "type-order graph allocation");
        if (allocated)
        {
            const uint32_t pointer = check_test_leaf(&graph, 0, (CheckValue){
                .type = pointer_array_int[0],
                .category = CHECK_RVALUE
            });
            const uint32_t first_array = check_graph_add(&graph, CHECK_OP_DEREFERENCE, 1, pointer, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint32_t one = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 1));
            // Postorder creates int* at level 2 before short* at level 1. A global level-order construction would
            // reverse their interned IDs, which a comparison in one previously populated table would conceal.
            check_graph_add(&graph, CHECK_OP_ADD, 3, first_array, one, BUSTER_CHECK_NONE);
            const uint32_t second_array = check_test_leaf(&graph, 4, (CheckValue){
                .type = array_short[0],
                .category = CHECK_LVALUE
            });
            check_graph_add(&graph, CHECK_OP_ADD, 5, second_array, one, BUSTER_CHECK_NONE);
            const CheckOptions options[3] = {
                {
                    .scalar = true
                },
                {
                    .scalar = false
                },
                {
                    .level_order = true
                },
            };
            CheckStream streams[3];
            const bool first = check_graph_run(&types[0].table, &graph, output, scratch, &options[0], &streams[0]);
            for (uint32_t path = 1; path < 3; path += 1)
            {
                const bool checked = check_graph_run(&types[path].table, &graph, output, scratch, &options[path], &streams[path]);
                check_test_expect(test, first && checked && check_test_stream_equal(&streams[0], &streams[path], 0), "scheduler changed newly interned TypeIds");
                check_test_expect(test, types[0].table.types.count == types[path].table.types.count, "scheduler constructed a different number of types");
            }
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, mark);
    check_test_types_close(&types[2]);
    check_test_types_close(&types[1]);
    check_test_types_close(&types[0]);
}

static __attribute__((noinline)) void check_test_graph_arguments(CheckTest* test, TypeTable* table, Arena* storage, Arena* output, Arena* scratch)
{
    const uint64_t mark = storage->length;
    CheckGraph graph;
    const bool initialized = check_graph_initialize(&graph, storage, 5, 3);
    check_test_expect(test, initialized, "call graph allocation");
    if (initialized)
    {
        const TypeId parameters[] = {
            BUSTER_TYPE_INT,
            BUSTER_TYPE_LONG
        };
        const TypeId function = type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE);
        const uint32_t callee = check_test_leaf(&graph, 0, (CheckValue){
            .type = function,
            .category = CHECK_FUNCTION
        });
        const uint32_t first = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_SHORT, 7));
        const uint32_t second = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 9));
        const uint32_t call = check_graph_add(&graph, CHECK_OP_CALL, 3, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.argument_nodes[0] = first;
        graph.argument_nodes[1] = second;
        graph.argument_count = 2;
        graph.argument_counts[call] = 2;
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const uint64_t output_mark = output->length;
        const uint64_t scratch_mark = scratch->length;
        const CheckOptions options = { 0 };
        CheckStream stream;
        bool valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, valid && stream.report_count == 0 && stream.argument_conversions[0].type == BUSTER_TYPE_INT &&
                               stream.argument_conversions[1].type == BUSTER_TYPE_LONG, "call graph retained argument conversions");
        arena_pop_to(output, output_mark);
        graph.argument_offsets[call] = UINT32_MAX;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "out-of-range argument offset rejected");
        graph.argument_offsets[call] = 1;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "argument slice extending past its count rejected");
        graph.argument_offsets[call] = 0;
        graph.argument_counts[call] = UINT32_MAX;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "overflowing argument count rejected");
        graph.argument_counts[call] = 2;
        graph.argument_nodes[1] = call;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "self-dependent call argument rejected");
        graph.argument_nodes[1] = second;
        uint32_t* nodes = graph.argument_nodes;
        graph.argument_nodes = 0;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "absent call argument storage rejected");
        graph.argument_nodes = nodes;
        const uint32_t second_call = check_graph_add(&graph, CHECK_OP_CALL, 4, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.argument_counts[second_call] = 2;
        graph.argument_offsets[second_call] = 0;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "calls cannot overlap argument conversion slices");
        graph.count -= 1;
        graph.argument_count = 3;
        graph.argument_nodes[2] = first;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "unowned trailing argument slot rejected");
        graph.argument_count = graph.argument_capacity + 1;
        valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, !valid && stream.status == CHECK_STATUS_INVALID_GRAPH, "argument logical count cannot exceed capacity");
        check_test_expect(test, output->length == output_mark && scratch->length == scratch_mark, "invalid call graph retained arena storage");
    }
    arena_pop_to(storage, mark);
}

static __attribute__((noinline)) void check_test_graph_guarded(CheckTest* test, TypeTable* table, Arena* output, Arena* scratch)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t widths[] = {
        sizeof(CheckOp),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(CheckValue),
        sizeof(TypeId),
        sizeof(uint32_t),
        sizeof(uint8_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
    };
    uint8_t* mappings[BUSTER_ARRAY_LENGTH(widths)];
    uint64_t readable[BUSTER_ARRAY_LENGTH(widths)];
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        readable[i] = (widths[i] * 33 + page - 1) / page * page;
        mappings[i] = mmap(0, readable[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = ready && mappings[i] != MAP_FAILED;
        if (mappings[i] != MAP_FAILED)
        {
            ready = mprotect(mappings[i] + readable[i], page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "guarded graph column allocation");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        // The leaves are the VALUE nodes' alone: every even node's.
        const uint32_t leaf_count = (count + 1) / 2;
        void* columns[BUSTER_ARRAY_LENGTH(widths)];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
        {
            columns[i] = mappings[i] + readable[i] - widths[i] * (i == 5 ? leaf_count : count);
        }
        CheckGraph graph = {
            .ops = columns[0],
            .tokens = columns[1],
            .operand0 = columns[2],
            .operand1 = columns[3],
            .operand2 = columns[4],
            .leaves = columns[5],
            .leaf_slots = columns[11],
            .leaf_count = leaf_count,
            .types = columns[6],
            .members = columns[7],
            .contexts = columns[8],
            .count = count,
            .capacity = count,
            .argument_offsets = columns[9],
            .argument_counts = columns[10],
        };
        for (uint32_t node = 0; node < count; node += 1)
        {
            const bool leaf = node == 0 || (node & 1) == 0;
            graph.ops[node] = leaf ? CHECK_OP_VALUE : CHECK_OP_ADD;
            graph.tokens[node] = node;
            graph.operand0[node] = leaf ? BUSTER_CHECK_NONE : node - 1;
            graph.operand1[node] = leaf ? BUSTER_CHECK_NONE : 0;
            graph.operand2[node] = BUSTER_CHECK_NONE;
            graph.leaf_slots[node] = leaf ? node / 2 : BUSTER_CHECK_NONE;
            if (leaf)
            {
                graph.leaves[node / 2] = check_test_integer(BUSTER_TYPE_UINT, node + 1);
            }
            graph.types[node] = 0;
            graph.members[node] = 0;
            graph.contexts[node] = 0;
            graph.argument_offsets[node] = 0;
            graph.argument_counts[node] = 0;
        }
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        graph.tokens = 0;
        graph.types = 0;
        graph.members = 0;
        graph.contexts = 0;
        graph.argument_offsets = 0;
        graph.argument_counts = 0;
        check_test_compare_graph(test, table, &graph, output, scratch, false);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], readable[i] + page);
        }
    }
}

typedef struct CheckSourceFixture CheckSourceFixture;
struct CheckSourceFixture
{
    const char* name;
    const char* source;
    bool rejected;
    bool unsupported;
    bool extension;
    // The C standard both sides check it in, GNU's dialect of it: 0 for gnu17, or 2023 for gnu23.
    uint32_t standard;
    // Whether both sides check it with plain char unsigned (-funsigned-char).
    bool char_unsigned;
    // An unsupported fixture that clang rejects, as what this can't decide: an error, or valid code whose own error comes
    // later. Its refusal is no later than clang's first error.
    bool undecided;
    // The type table's room, for a fixture larger than check_test_types_open's; null for that.
    const TypeCapacity* capacity;
    // Optional sum of scalar and vector reader/checker times, excluding fixture setup and comparisons.
    double* check_seconds;
};

static uint32_t check_test_source_line(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    uint32_t line = 0;
    if (token < parser->tokens.count)
    {
        const uint32_t location = pp->output.locations[parser->tokens.sources[token]];
        const char* name = 0;
        uint32_t length = 0;
        line = pp_presumed(pp, location, &name, &length);
    }
    return line;
}

static void check_test_input_recorded(CheckTest* test, const CheckGraph* graph, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint8_t* pure = graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)graph->count + 1) : NULL;
    const bool room = graph->count == 0 || pure != NULL;
    check_test_expect(test, room && graph->recorded_ready && graph->recorded_nodes == graph->count,
                      "source reader publishes complete graph facts even for a diagnostic-only prefix");
    for (uint32_t scalar = 0; room && scalar < 2; scalar += 1)
    {
        CheckGraphValidation validation;
        uint32_t failed;
        const bool valid = scalar ? check_graph_validate(graph, pure, &validation, &failed) :
                                    check_graph_validate_vector(graph, pure, &validation, &failed);
        check_test_expect(test, valid && check_graph_recorded_equal(graph, &validation, pure, &failed),
                          "every source fixture's reader facts equal both independent validation paths");
    }
    arena_pop_to(scratch, mark);
}

static void check_test_source(CheckTest* test, const CheckSourceFixture* fixture, const char* path, const ParseTestClang* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions pp_options = {
        .language = {
            .standard = fixture->standard != 0 ? fixture->standard : 2017,
            .gnu = true
        },
        .no_standard_includes = true,
        .semantic_pragmas = true,
        .predefines.char_unsigned = fixture->char_unsigned,
    };
    const ParseOptions parser_options[2] = {
        {
            .scalar = true
        },
        {
            .scalar = false
        },
    };
    CheckTestTypes types[2] = { 0 };
    Arena input_storage[2] = { 0 };
    Arena output_storage[2] = { 0 };
    Arena scratch[2] = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    for (uint32_t p = 0; p < 2; p += 1)
    {
        ready = ready && parse_initialize(&parsers[p], &parser_options[p], pp_options.language) &&
                (fixture->capacity != NULL ? check_test_types_open_with(&types[p], *fixture->capacity) : check_test_types_open(&types[p]));
        // Room for check_test_initializer_nesting's 300,000 levels; only what a fixture uses is touched.
        input_storage[p] = arena_reserve(UINT64_C(1) << 28);
        output_storage[p] = arena_reserve(UINT64_C(1) << 28);
        scratch[p] = arena_reserve(UINT64_C(1) << 28);
        ready = ready && input_storage[p].base != 0 && output_storage[p].base != 0 && scratch[p].base != 0;
    }
    ready = ready && pp_run_source(&pp, path, (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source));
    check_test_expect(test, ready, "source fixture initialization/preprocessing");
    CheckInput input[2] = { 0 };
    CheckStream streams[2] = { 0 };
    bool rejected[2] = {
        false,
        false
    };
    bool checked[2] = {
        false,
        false
    };
    bool unsupported[2] = {
        false,
        false
    };
    uint32_t lines[2] = { 0 };
    for (uint32_t p = 0; ready && p < 2; p += 1)
    {
        parse_translation_unit(&parsers[p], &pp);
        const ParseTestOutcome outcome = parse_test_outcome(&parsers[p], &pp);
        rejected[p] = !outcome.parsed;
        lines[p] = outcome.line;
        if (!rejected[p])
        {
            struct timespec reader_start;
            struct timespec reader_stop;
            if (fixture->check_seconds != NULL)
            {
                clock_gettime(CLOCK_MONOTONIC, &reader_start);
            }
            const bool adapted = check_input(&input[p], &parsers[p], &pp, &types[p].table, &input_storage[p], &scratch[p]);
            if (fixture->check_seconds != NULL)
            {
                clock_gettime(CLOCK_MONOTONIC, &reader_stop);
                *fixture->check_seconds += (double)(reader_stop.tv_sec - reader_start.tv_sec) + (double)(reader_stop.tv_nsec - reader_start.tv_nsec) * 1e-9;
            }
            if (adapted || input[p].diagnostic_graph)
            {
                check_test_input_recorded(test, &input[p].graph, &scratch[p]);
            }
            char resource_message[256];
            snprintf(resource_message, sizeof(resource_message), "%s: %s source adapter exhausted fixture resources", fixture->name, p == 0 ? "scalar" : "vector");
            check_test_expect(test, !input[p].resource, resource_message);
            snprintf(resource_message, sizeof(resource_message), "%s: the %s path acts at %u tokens the vector path's walks would skip", fixture->name,
                     p == 0 ? "scalar" : "vector", input[p].visit_misses);
            check_test_expect(test, input[p].visit_misses == 0, resource_message);
            if (p == 1 && adapted && !rejected[0] && input[0].token_count >= 16)
            {
                // The selected walks skip tokens (#529), independently of the host's classification kernels.
                snprintf(resource_message, sizeof(resource_message), "%s: the selected walks visit %u and %u of %u tokens, the scalar path's %u and %u",
                         fixture->name, input[1].resolve_visits, input[1].expression_visits, input[1].token_count, input[0].resolve_visits, input[0].expression_visits);
                const bool visits = input[1].resolve_visits < input[0].resolve_visits && input[1].expression_visits < input[0].expression_visits;
                check_test_expect(test, visits, resource_message);
            }
            rejected[p] = !adapted;
            uint32_t first = BUSTER_CHECK_NONE;
            for (uint32_t i = 0; i < input[p].report_count; i += 1)
            {
                const CheckInputReport* report = &input[p].reports[i];
                const uint32_t source = check_diagnostic_input_report_source(&input[p], &parsers[p], report);
                first = source < first ? source : first;
                unsupported[p] |= report->message == CHECK_INPUT_UNSUPPORTED || report->message == CHECK_INPUT_WIDE_LITERAL;
            }
            if (adapted)
            {
                const CheckOptions options = {
                    .scalar = p == 0,
                    .verify = true
                };
                struct timespec start;
                struct timespec stop;
                if (fixture->check_seconds != NULL)
                {
                    clock_gettime(CLOCK_MONOTONIC, &start);
                }
                checked[p] = check_graph_run(&types[p].table, &input[p].graph, &output_storage[p], &scratch[p], &options, &streams[p]);
                if (fixture->check_seconds != NULL)
                {
                    clock_gettime(CLOCK_MONOTONIC, &stop);
                    *fixture->check_seconds += (double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_nsec - start.tv_nsec) * 1e-9;
                }
                check_test_expect(test, checked[p], "source adapter emitted an invalid graph");
                rejected[p] = !checked[p] || streams[p].report_count != 0;
                if (checked[p] && streams[p].report_count != 0)
                {
                    const uint32_t token = streams[p].reports[0].token;
                    const uint32_t source = token < parsers[p].tokens.count ? parsers[p].tokens.sources[token] : BUSTER_CHECK_NONE;
                    first = source < first ? source : first;
                }
                if (checked[p] && streams[p].report_count == 0)
                {
                    CheckInitializers initializers = { 0 };
                    const bool initialized = check_initializers_run(&initializers, &input[p], &streams[p], &parsers[p], &types[p].table,
                                                                   &output_storage[p], &scratch[p]);
                    check_test_expect(test, initializers.status != CHECK_INIT_RESOURCE, "source initializer fixture exhausted resources");
                    rejected[p] |= !initialized;
                    unsupported[p] |= initializers.status == CHECK_INIT_UNSUPPORTED;
                    if (!initialized)
                    {
                        const uint32_t token = initializers.token;
                        const uint32_t source = token < parsers[p].tokens.count ? parsers[p].tokens.sources[token] : BUSTER_CHECK_NONE;
                        first = source < first ? source : first;
                    }
                    // The musttail returns' errors (#147, c_check_musttail.c), as the driver reports them after the expressions'.
                    CheckMusttails musttails = { 0 };
                    const bool tails = check_musttail_run(&musttails, &input[p], &parsers[p], &streams[p], &types[p].table, &output_storage[p], &scratch[p]);
                    check_test_expect(test, tails, "source musttail fixture exhausted resources");
                    rejected[p] |= musttails.count != 0;
                    for (uint32_t m = 0; m < musttails.count; m += 1)
                    {
                        const CheckMusttailDiagnostic* diagnostic = musttails.diagnostics + m;
                        unsupported[p] |= diagnostic->error == CHECK_MUSTTAIL_BUILTIN;
                        const uint32_t source = diagnostic->token < parsers[p].tokens.count ? parsers[p].tokens.sources[diagnostic->token] : BUSTER_CHECK_NONE;
                        first = source < first ? source : first;
                    }
                }
            }
            const char* name = NULL;
            uint32_t length = 0;
            lines[p] = first < pp.output.count ? pp_presumed(&pp, pp.output.locations[first], &name, &length) : 0;
        }
        char description[320];
        snprintf(description, sizeof(description), "%s: %s expected, %s from %s path, first line %u (clang %u)", fixture->name,
                 fixture->rejected ? "reject" : "accept", rejected[p] ? "reject" : "accept", p == 0 ? "scalar" : "vector", lines[p], clang->line);
        // A refusal is the fixture's kind: an unsupported construct for an unsupported fixture, and otherwise an error.
        check_test_expect(test, rejected[p] == fixture->rejected && (!fixture->rejected || fixture->unsupported == unsupported[p]), description);
        if (!fixture->unsupported && !fixture->extension)
        {
            check_test_expect(test, rejected[p] == clang->rejected && (!rejected[p] || lines[p] == clang->line), description);
        }
        else if (fixture->undecided)
        {
            check_test_expect(test, clang->rejected && lines[p] <= clang->line, description);
        }
    }
    if (ready)
    {
        check_test_expect(test, rejected[0] == rejected[1] && lines[0] == lines[1], "source paths disagree on acceptance or first-error line");
        if (checked[0] && checked[1])
        {
            check_test_expect(test, input[0].graph.argument_count == input[1].graph.argument_count &&
                                   check_test_stream_equal(&streams[0], &streams[1], input[0].graph.argument_count), "source paths produce different typed streams");
        }
    }
    for (uint32_t p = 0; p < 2; p += 1)
    {
        arena_release(&scratch[p]);
        arena_release(&output_storage[p]);
        arena_release(&input_storage[p]);
        check_test_types_close(&types[p]);
        parse_release(&parsers[p]);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static __attribute__((noinline)) void check_test_sources(CheckTest* test, const char* clang)
{
    const CheckSourceFixture fixtures[] = {
        {
            "empty translation unit",
            "",
            false,
            false
        },
        {
            "global declaration",
            "int x;\n",
            false,
            false
        },
        {
            "return integer arithmetic",
            "int f(void) { return 2 + 3 * 4; }\n",
            false,
            false
        },
        {
            "parameter promotions",
            "int f(unsigned short a, signed char b) { return a + b; }\n",
            false,
            false
        },
        {
            "typedef declaration",
            "typedef unsigned long Size; Size f(Size n) { return n + 1; }\n",
            false,
            false
        },
        {
            "local initialization",
            "int f(int a) { int b = a + 1; return b; }\n",
            false,
            false
        },
        {
            "const initialization",
            "int f(int a) { const int b = a; return b; }\n",
            false,
            false
        },
        {
            "local scope shadowing",
            "int f(int x) { { long x = 7; x = x + 1; } return x; }\n",
            false,
            false
        },
        {
            "pointer read",
            "int f(const int *p) { return *p; }\n",
            false,
            false
        },
        {
            "pointer subscript",
            "int f(int *p) { return p[2]; }\n",
            false,
            false
        },
        {
            "pointer qualification",
            "int f(int *p) { const int *q = p; return *q; }\n",
            false,
            false
        },
        {
            "global address initializer",
            "int g; int *p = &g;\n",
            false,
            false
        },
        {
            "global array initializer",
            "int a[3]; int *p = a;\n",
            false,
            false
        },
        {
            "address cancels void pointer indirection",
            "void *p; void *f(void) { return &*p; }\n",
            false,
            false
        },
        {
            "address cancels null pointer indirection",
            "void *p = &*(void *)0;\n",
            false,
            false
        },
        {
            "address cancels absolute pointer indirection",
            "int *p = &*(int *)4;\n",
            false,
            false
        },
        {
            "register pointer does not make its pointee register",
            "int *f(register int *p) { return &*p; }\n",
            false,
            false
        },
        {
            "grouped comma expression",
            "int f(void) { return (1, 2); }\n",
            false,
            false
        },
        {
            "comma condition",
            "int f(int x) { if (x = 0, x) { return 1; } return 2; }\n",
            false,
            false
        },
        {
            "comma subscript",
            "int f(int *p) { return p[0, 1]; }\n",
            false,
            false
        },
        {
            "local static address initializer",
            "void f(void) { static int x; static int *p = &x; }\n",
            false,
            false
        },
        {
            "null pointer assignment",
            "int *p = 0;\n",
            false,
            false
        },
        {
            "constant static initializer",
            "unsigned long x = 0xffffffffu + 1u;\n",
            false,
            false
        },
        {
            "floating return conversion",
            "double f(float x) { return x + 2.5; }\n",
            false,
            false
        },
        {
            "sizeof expression",
            "unsigned long f(void) { return sizeof(1 / 0); }\n",
            false,
            false
        },
        {
            "sizeof constant array",
            "unsigned long f(void) { int a[3]; return sizeof a; }\n",
            false,
            false
        },
        {
            "call arguments",
            "int f(int a, long b); int g(void) { return f(1, 2); }\n",
            false,
            false
        },
        {
            "short-circuit constant",
            "_Static_assert(1 || (1 / 0), \"short circuit\");\n",
            false,
            false
        },
        {
            "GNU folded-constant extension",
            "int f(void); _Static_assert(1 || f(), \"unevaluated call\");\n",
            false,
            false
        },
        {
            "conditional constant",
            "_Static_assert((0 ? 1 / 0 : 17) == 17, \"conditional\");\n",
            false,
            false
        },
        {
            "floating immediate ICE cast",
            "_Static_assert((int)3.75 == 3, \"cast\");\n",
            false,
            false
        },
        {
            "C integer literal types",
            "_Static_assert(sizeof(2147483648) == 8 && sizeof(0xffffffff) == 4, \"literal rank\");\n",
            false,
            false
        },
        {
            "undeclared identifier",
            "int f(void)\n{\n return absent;\n}\n",
            true,
            false
        },
        {
            "scope no longer visible",
            "int f(void) { { int x = 1; }\n return x; }\n",
            true,
            false
        },
        {
            "const assignment",
            "int f(void)\n{ const int x = 1;\n x = 2; return x; }\n",
            true,
            false
        },
        {
            "const parameter assignment",
            "int f(const int x) { x = 2; return x; }\n",
            true,
            false
        },
        {
            "register parameter address",
            "int *f(register int x) { return &x; }\n",
            true,
            false
        },
        {
            "register local address",
            "int *f(void) { register int x; return &x; }\n",
            true,
            false
        },
        {
            "register address keeps first failing source line",
            "int f(void) { register int x;\n return\n *\n &x;\n}\n",
            true,
            false
        },
        {
            "discarded register array still decays",
            "int f(void) { register int a[2]; a; return 0; }\n",
            true,
            false
        },
        {
            "GNU discarded void indirection",
            "void f(void *p) { *p; }\n",
            false,
            false
        },
        {
            "floating remainder",
            "int f(void)\n{\n return 1.0 % 2;\n}\n",
            true,
            false
        },
        {
            "address rvalue",
            "int *f(void) { return &1; }\n",
            true,
            false
        },
        {
            "dereference integer",
            "int f(void) { return *1; }\n",
            true,
            false
        },
        {
            "incompatible pointer initialization",
            "int *p = 1;\n",
            true,
            false
        },
        {
            "missing call argument",
            "int f(int); int g(void)\n{ return f(); }\n",
            true,
            false
        },
        {
            "calling nonfunction",
            "int f(void) { int x = 1; return x(); }\n",
            true,
            false
        },
        {
            "false static assertion",
            "_Static_assert(0, \"false\");\n",
            true,
            false
        },
        {
            "division in required ICE",
            "_Static_assert(1 / 0, \"division\");\n",
            true,
            false
        },
        {
            "nonconstant static initializer",
            "int y;\nint x = y;\n",
            true,
            false
        },
        {
            "automatic address static initializer",
            "void f(void) { int x;\n static int *p = &x; }\n",
            true,
            false
        },
        {
            "duplicate global definition",
            "int x = 1;\nint x = 2;\n",
            true,
            false
        },
        {
            "duplicate function definition",
            "int f(void) { return 1; }\nint f(void) { return 2; }\n",
            true,
            false
        },
        {
            "retained prior prototype",
            "int f(int); int f();\nint g(void) { return f(); }\n",
            true,
            false
        },
        {
            "duplicate parameter name",
            "int f(int x, int x);\n",
            true,
            false
        },
        {
            "restrict requires pointer to object",
            "typedef int F(void); F *restrict p;\n",
            true,
            false
        },
        {
            "definition conflicts with prior prototype",
            "int f(int);\nint f() { return 1; }\n",
            true,
            false
        },
        {
            "invalid integer suffix",
            "int x = 1uu;\n",
            true,
            false
        },
        {
            "basic universal character name before C23",
            "_Static_assert('\\u0041' == 65, \"basic UCN\");\n",
            true,
            false
        },
        {
            "record declaration",
            "struct S { int x; }; struct S value;\n",
            false,
            false
        },
        {
            // A GNU extension clang takes (a warning under -pedantic alone), with clang's layouts.
            "struct whose only named member is a flexible array member",
            "struct T { int a[]; }; struct U { int : 0; char c[]; }; struct V { char : 3; long l[]; };\n"
            "struct W { struct T t; int b; }; struct T *p; int n = sizeof(struct T) + sizeof(struct U);\n"
            "_Static_assert(sizeof(struct T) == 0 && _Alignof(struct T) == 4, \"T\");\n"
            "_Static_assert(sizeof(struct U) == 0 && _Alignof(struct U) == 1, \"U\");\n"
            "_Static_assert(sizeof(struct V) == 8 && _Alignof(struct V) == 8 && __builtin_offsetof(struct V, l) == 8, \"V\");\n"
            "_Static_assert(sizeof(struct W) == 4, \"W\");\n",
            false,
            false
        },
        {
            "flexible array member before another member",
            "struct S { int a[]; int b; };\n",
            true,
            false
        },
        {
            // C23's fixed underlying type, which clang takes in every mode: complete at its base (sizeof in its own list),
            // its enumerators of its own type, the base's qualifiers dropped, an attribute before its name applied at the
            // base, packed ignored, its declarations alone (here in a block, a new enum) keeping or making it.
            "enum with a fixed underlying type",
            "enum E : unsigned char { A = 255, B = 0 }; enum E : unsigned char; typedef unsigned int U;\n"
            "_Static_assert(sizeof(enum E) == 1 && _Alignof(enum E) == 1 && _Generic(A, enum E: 1, default: 0) && _Generic(+A, int: 1, default: 0), \"E\");\n"
            "enum F : long long { C = -1, D }; enum G : short; enum G : short { H = sizeof(enum G) }; enum I : const U { J = 4294967295u };\n"
            "_Static_assert(sizeof(enum F) == 8 && D == 0 && H == 2 && J == 4294967295u && sizeof(enum I) == 4, \"F G I\");\n"
            "enum K : _Bool { L, M }; enum N : signed char { O = -128, P = 127 }; enum : unsigned long { Q = 1ul << 63 }; enum R : unsigned { S = -1 };\n"
            "_Static_assert(M == 1 && sizeof(enum K) == 1 && Q == 9223372036854775808ul && S == 4294967295u, \"K Q S\");\n"
            "enum __attribute__((aligned(8))) T : char { V }; enum __attribute__((packed)) W : int { X }; enum Y : short { Z } __attribute__((packed));\n"
            "_Static_assert(_Alignof(enum T) == 8 && sizeof(enum T) == 1 && sizeof(enum W) == 4 && sizeof(enum Y) == 2, \"T W Y\");\n"
            "struct St { enum E e : 4; enum E f : 8; enum F g; }; _Static_assert(sizeof(struct St) == 16, \"St\");\n"
            "int f(enum E e) { switch (e) { case A: return 1; case B: return 2; default: return 3; } }\n"
            "unsigned g(void) { enum E e = A; enum E : unsigned char; return e + 1u; }\n"
            "int h(enum N n) { return n == O ? 1 : n; }\n",
            false,
            false
        },
        {
            // A fixed enum's enumerators are of its base's type in its list, and after it of the enum's in C23 alone, as
            // clang 23 types them (#428's review): before C23, two such enums' enumerators of one base are of one type.
            "fixed enum: enumerators of the base's type before C23",
            "enum E : int { A, B = _Generic(A, int: 2, default: 3) }; enum F : int { C };\n"
            "_Static_assert(B == 2 && _Generic(A, __typeof__(C): 1, default: 0) == 1 && _Generic(A, int: 1, default: 0), \"C17\");\n"
            "enum G : unsigned char { D }; _Static_assert(_Generic(D, unsigned char: 1, default: 0) && _Generic(+D, int: 1, default: 0), \"G\");\n",
            false,
            false
        },
        {
            "fixed enum: enumerators of the enum's type after its list in C23",
            "enum E : int { A, B = _Generic(A, int: 2, default: 3) }; enum F : int { C };\n"
            "_Static_assert(B == 2 && _Generic(A, __typeof__(C): 1, default: 0) == 0 && _Generic(A, enum E: 1, default: 0), \"C23\");\n"
            "enum G : unsigned char { D }; _Static_assert(_Generic(D, enum G: 1, default: 0) && _Generic(+D, int: 1, default: 0), \"G\");\n",
            false,
            false,
            .standard = 2023
        },
        {
            // The same assertion in the other dialect: each must refuse it.
            "fixed enum: C23's enumerator types refused before it",
            "enum E : int { A }; enum F : int { C }; _Static_assert(_Generic(A, __typeof__(C): 1, default: 0) == 0, \"C17\");\n",
            true,
            false
        },
        {
            "fixed enum: earlier enumerator types refused in C23",
            "enum E : int { A }; enum F : int { C }; _Static_assert(_Generic(A, __typeof__(C): 1, default: 0) == 1, \"C23\");\n",
            true,
            false,
            .standard = 2023
        },
        {
            // A negative value's significant bits, its sign not checked (clang's isRepresentableIntegerValue), and a
            // value's active bits, less the sign bit of a signed type.
            "fixed enum: an enumerator past its type",
            "enum E : unsigned char {\n  A = 256\n};\n",
            true,
            false
        },
        {
            "fixed enum: the successor past its type",
            "enum E : signed char {\n  A = 127,\n  B\n};\n",
            true,
            false
        },
        {
            "fixed enum: redeclared with another type",
            "enum E : int;\nenum E : long { A };\n",
            true,
            false
        },
        {
            "fixed enum: declared with a declarator",
            "int a;\nenum E : int x;\n",
            true,
            false
        },
        {
            "fixed enum: an enum without one given one",
            "enum E { A };\nenum E : int;\n",
            true,
            false
        },
        {
            "fixed enum: one with it defined without it",
            "enum E : int;\nenum E { A };\n",
            true,
            false
        },
        {
            "fixed enum: defined twice",
            "enum E : int { A };\nenum E : int { B };\n",
            true,
            false
        },
        {
            "fixed enum: a floating type",
            "int a;\nenum E : float { A };\n",
            true,
            false
        },
        {
            // GNU attributes after a bit-field's width, the member's, as clang (and GCC) lay them out: aligned, packed (a
            // compound width), two groups, an unnamed field's and an empty one; then widths of every form a width takes,
            // which the parser's tree keeps whole apart from the attributes: _Generic alone and in a sum (#430's review),
            // sizeof, a cast of a floating constant, __builtin_offsetof in ?: and __builtin_choose_expr.
            "attributes after a bit-field's width",
            "enum { N = 5 };\n"
            "struct S { int a : 3 __attribute__((aligned(4))), b : 2; char c; };\n"
            "struct T { char x; unsigned long y : 1 + 2 __attribute__((packed)); char z; };\n"
            "struct U { char x; int y : N __attribute__((aligned(16))) __attribute__((unused)); char z : (N - 1) __attribute__(()); };\n"
            "struct V { char x; long : 0 __attribute__((aligned(8))); char y; };\n"
            "struct O { char a; int b; };\n"
            "struct W { char x; unsigned a : _Generic(0, int: 3, default: 1) __attribute__((packed));\n"
            "           unsigned b : _Generic(0L, int: 1, long: 2) + 3 __attribute__((aligned(8))); char y; };\n"
            "struct X { char x; unsigned a : sizeof(short) * 2 __attribute__((aligned(4))), b : (int)2.5 + N __attribute__((packed));\n"
            "           unsigned c : __builtin_offsetof(struct O, b) ? 6 : 1 __attribute__(()); unsigned d : __builtin_choose_expr(1, 7, 1.0) __attribute__((aligned(2))); };\n"
            "_Static_assert(sizeof(struct S) == 4 && _Alignof(struct S) == 4, \"S\");\n"
            "_Static_assert(sizeof(struct T) == 3 && _Alignof(struct T) == 1, \"T\");\n"
            "_Static_assert(sizeof(struct U) == 32 && _Alignof(struct U) == 16, \"U\");\n"
            "_Static_assert(sizeof(struct V) == 9 && _Alignof(struct V) == 1, \"V\");\n"
            "_Static_assert(sizeof(struct W) == 16 && _Alignof(struct W) == 8, \"W\");\n"
            "_Static_assert(sizeof(struct X) == 12 && _Alignof(struct X) == 4, \"X\");\n",
            false,
            false
        },
        {
            // Its alignment applied after its body, which the enum's completion at its base came before: refused.
            "fixed enum: aligned after its body",
            "enum F : char { B } __attribute__((aligned(8)));\n",
            true,
            true
        },
        {
            // #428's review: GNU attributes among the base's specifiers, which clang takes as the base type's and ignores
            // (the base's own type kept), with a body, without and before a typedef's declarator; and the enum's
            // references (`enum E x`) and declarations alone in another scope, which need no base.
            "fixed enum: attributes in its base, and references without it",
            "enum A : int __attribute__((unused)); enum B : int __attribute__((aligned(8))) { B0 };\n"
            "typedef enum C : __attribute__((unused)) unsigned __attribute__((unused)) char __attribute__((mode(DI))) { C0 } T; T t = C0;\n"
            "_Static_assert(sizeof(enum B) == 4 && _Alignof(enum B) == 4 && sizeof(T) == 1, \"B T\");\n"
            "enum D : short; enum D d; enum D : short; void f(void) { enum D; enum D *p = 0; (void)p; }\n",
            false,
            false
        },
        {
            // Attributes after a tag's name are the declaration's, not the tag's: before a `;` they make it a reference to
            // the visible tag, not a new one in a block's scope, and not a declaration of a fixed enum alone, as clang has it.
            "attributes after a tag's name: a reference",
            "struct S { int a; };\n"
            "void f(void) { struct S __attribute__((unused)); struct S s; s.a = 1; }\n"
            "enum K : int; enum K __attribute__((unused)); void g(void) { enum K __attribute__((unused)); enum K k = 0; (void)k; }\n",
            false,
            false
        },
        {
            // A declaration of it alone without its base, in its scope, before its body or after it: clang's
            // "enumeration previously declared with fixed underlying type", at the name.
            "fixed enum: declared alone without it",
            "enum E : int;\nenum E;\n",
            true,
            false
        },
        {
            "fixed enum: defined, then declared alone without it",
            "enum E : int { A };\nenum E;\n",
            true,
            false
        },
        {
            // restrict needs a pointer; const and volatile are dropped.
            "fixed enum: restrict in its base",
            "int a;\nenum E : restrict int { A };\n",
            true,
            false
        },
        {
            // An attribute that makes the base a vector's: not an integer type.
            "fixed enum: a vector in its base",
            "int a;\nenum E : int __attribute__((vector_size(16))) { A };\n",
            true,
            false
        },
        {
            // GNU C's labels as values (#133, Lua's interpreter): a static table of addresses of labels, a computed goto
            // through it, through a local and through ?:, the addresses compared and stored, and a computed goto out of a
            // variable-length array's block, which leaves its scope as clang lets it.
            "labels as values",
            "int f(int n, const unsigned char *code) {\n"
            "    static const void *const table[] = { &&add, &&twice, &&done };\n"
            "    void *last = &&done;\n"
            "    int acc = n;\n"
            "    goto *table[*code];\n"
            "add:\n"
            "    acc += code[1];\n"
            "    code += 2;\n"
            "    goto *table[*code];\n"
            "twice:\n"
            "    acc += acc;\n"
            "    last = &&twice;\n"
            "    code += 1;\n"
            "    goto *(acc > 100 ? table[2] : table[*code]);\n"
            "done:\n"
            "    return last == &&twice ? acc : -acc;\n"
            "}\n"
            "int g(int n) {\n"
            "    void *out = &&end;\n"
            "    for (int i = 0; i < 3; i++) { int v[(n & 3) + 1]; v[0] = i; if (v[0] == 2) goto *out; }\n"
            "    n = 0;\n"
            "end:\n"
            "    return n;\n"
            "}\n",
            false,
            false
        },
        {
            // An address of a label no function has: clang's "use of undeclared label", at the name.
            "labels as values: an undeclared label",
            "void f(void) {\n"
            "    void *p = &&here;\n"
            "    void *q = &&nowhere;\n"
            "    goto *p;\n"
            "here:\n"
            "    (void)q;\n"
            "}\n",
            true,
            false
        },
        {
            // An address of a label outside a function's body.
            "labels as values: outside a function",
            "void f(void) { x: ; }\n"
            "void *p = &&x;\n",
            true,
            false
        },
        {
            // A computed goto in a function that takes no label's address.
            "labels as values: a computed goto without labels",
            "void f(void *p) {\n"
            "    goto *p;\n"
            "}\n",
            true,
            false
        },
        {
            // A computed goto may go to any of its function's labels: not into a variable-length array's scope, nor into a
            // statement expression.
            "labels as values: into a variable-length array's scope",
            "void f(int n) {\n"
            "    void *p = &&inner;\n"
            "    goto *p;\n"
            "    { int v[n]; inner: v[0] = 1; }\n"
            "}\n",
            true,
            false
        },
        {
            "labels as values: into a statement expression",
            "int f(void) {\n"
            "    void *p = &&inner;\n"
            "    goto *p;\n"
            "    return ({ inner: 1; });\n"
            "}\n",
            true,
            false
        },
        {
            // goto *'s operand converts to const void * as an assignment would: an integer is clang's error.
            "labels as values: a computed goto of an integer",
            "void f(int n) {\n"
            "    void *p = &&here;\n"
            "    goto *n;\n"
            "here:\n"
            "    (void)p;\n"
            "}\n",
            true,
            false
        },
        {
            // goto *'s operand is the whole expression after the `*` (#431's review), not the `*`'s operand: a conditional
            // of an int's condition, one of a void *'s sum, a comma and an assignment, all of them pointers.
            "labels as values: a computed goto's whole expression",
            "int f(int n, void *p, int k) {\n"
            "    void *t[2] = { &&even, &&odd };\n"
            "    goto *(n & 1) ? t[1] : t[0];\n"
            "even:\n"
            "    goto *k ? p + 0 : &&odd;\n"
            "odd:\n"
            "    goto *n++, &&done;\n"
            "done:\n"
            "    goto *p = &&end;\n"
            "end:\n"
            "    return n;\n"
            "}\n",
            false,
            false
        },
        {
            // And a conditional of integers is an integer, clang's error.
            "labels as values: a computed goto of a conditional's integer",
            "void f(void *p) {\n"
            "    void *q = &&here;\n"
            "    goto *p ? 1 : 0;\n"
            "here:\n"
            "    (void)q;\n"
            "}\n",
            true,
            false
        },
        // GNU C's local labels (#147): a `__label__` list's names are labels of its block, which shadow the function's
        // and an outer list's, in a statement expression a macro expands twice too, whose labeled last statement gives
        // its value.
        {
            "local labels: a statement expression's, expanded twice",
            "#define CHECKED(x) ({ __label__ out; int r = 0; if (x) goto out; r = 1; out: r; })\n"
            "int f(int a) {\n"
            "    return CHECKED(a) + CHECKED(a > 1);\n"
            "}\n",
            false,
            false
        },
        {
            "local labels: nested blocks' and the function's",
            "int f(int n) {\n"
            "    int s = 0;\n"
            "    {\n"
            "        __label__ again, done;\n"
            "    again:\n"
            "        if (n-- > 0) { s += 1; goto again; }\n"
            "        {\n"
            "            __label__ again;\n"
            "        again:\n"
            "            if (s > 100) { s -= 1; goto again; }\n"
            "        }\n"
            "        goto done;\n"
            "    done:;\n"
            "    }\n"
            "    if (s == 0) goto again;\n"
            "    return s;\n"
            "again:\n"
            "    return -s;\n"
            "}\n",
            false,
            false
        },
        {
            "local labels: an address of one",
            "int f(int n) {\n"
            "    {\n"
            "        __label__ odd;\n"
            "        void *p = &&odd;\n"
            "        if (n & 1) goto *p;\n"
            "        return 0;\n"
            "    odd:\n"
            "        return 1;\n"
            "    }\n"
            "}\n",
            false,
            false
        },
        {
            // A local label its block doesn't define: clang's "use of undeclared label", at its name in the list.
            "local labels: one its block doesn't define",
            "void f(void) {\n"
            "    {\n"
            "        __label__ missing;\n"
            "        (void)0;\n"
            "    }\n"
            "}\n",
            true,
            false
        },
        {
            // A goto from outside the block reaches no label of the function.
            "local labels: a goto from outside the block",
            "void f(void) {\n"
            "    goto inner;\n"
            "    {\n"
            "        __label__ inner;\n"
            "    inner:;\n"
            "    }\n"
            "}\n",
            true,
            false
        },
        {
            "local labels: one defined twice",
            "void f(void) {\n"
            "    {\n"
            "        __label__ a;\n"
            "    a:;\n"
            "    a:;\n"
            "    }\n"
            "}\n",
            true,
            false
        },
        {
            "local labels: the function's after the block",
            "void f(void) {\n"
            "    {\n"
            "        __label__ a;\n"
            "    a:;\n"
            "    }\n"
            "    goto a;\n"
            "}\n",
            true,
            false
        },
        // GNU C's __auto_type (#147): a variable's type is its initializer's, lvalue-converted and decayed, with the
        // specifiers' qualifiers, as clang deduces it.
        {
            "__auto_type: a statement expression's maximum",
            "#define MAX(a, b) ({ __auto_type a_ = (a); __auto_type b_ = (b); a_ > b_ ? a_ : b_; })\n"
            "long f(int x, long y) {\n"
            "    __auto_type m = MAX(x, y);\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(m), long), \"the conditional's type\");\n"
            "    return m + MAX(MAX(x, 1), 2);\n"
            "}\n",
            false,
            false
        },
        {
            "__auto_type: lvalue conversion and decay",
            "int g(void);\n"
            "const char *h(void) {\n"
            "    const int c = 1;\n"
            "    int a[3] = { 0 };\n"
            "    volatile short v = 2;\n"
            "    _Atomic int t = 3;\n"
            "    __auto_type x = c;\n"
            "    __auto_type p = a;\n"
            "    __auto_type q = g;\n"
            "    __auto_type s = \"abc\";\n"
            "    __auto_type w = v;\n"
            "    __auto_type z = t;\n"
            "    const __auto_type k = 2.0f;\n"
            "    __auto_type volatile u = (char)1 + (char)2;\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(x), int), \"lvalue conversion\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), \"an array's decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(q), int (*)(void)), \"a function's decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(s), char *), \"a string's decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(w), short), \"volatile dropped\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(z), _Atomic int), \"_Atomic kept\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(k), const float), \"the specifiers' const\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(u), volatile int), \"promoted, and volatile\");\n"
            "    x = 3;\n"
            "    p[0] = x + q() + w + z + (int)k + u;\n"
            "    return s;\n"
            "}\n",
            false,
            false
        },
        {
            "__auto_type: file scope, static, a loop's, structures and compound literals",
            "typedef struct { int a; } S;\n"
            "S make(void);\n"
            "__auto_type g = 1L;\n"
            "static __auto_type h = 'c';\n"
            "int f(void) {\n"
            "    static __auto_type n = sizeof g;\n"
            "    __auto_type s = make();\n"
            "    __auto_type t = (S){ 2 };\n"
            "    __auto_type e = (int[]){ 1, 2, 3 };\n"
            "    __auto_type r = ({ __auto_type r = 4; r + 1; });\n"
            "    int sum = s.a + t.a + h + e[2] + r;\n"
            "    for (__auto_type i = 0u; i < n; i++) sum += (int)i;\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(g), long), \"file scope\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(h), int), \"a character constant's int\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(n), unsigned long), \"sizeof's size_t\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(s), S), \"a structure\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(t), S), \"a compound literal\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(e), int *), \"an array's compound literal decays\");\n"
            "    __auto_type l = ({ int k = sum; if (k > 3) goto done; k = 5; done: k * 2L; });\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(l), long), \"a labeled last statement's\");\n"
            "    extern long g;\n"
            "    return sum + (int)l + (int)g;\n"
            "}\n",
            false,
            false
        },
        {
            // A use of the variable in its own initializer, though an outer one has its name: at the use.
            "__auto_type: its own initializer names it",
            "void f(void) {\n"
            "    int x = 1;\n"
            "    {\n"
            "        __auto_type x =\n"
            "            x + 1;\n"
            "    }\n"
            "}\n",
            true,
            false
        },
        {
            // A use of it in a type query of its own initializer, which the walk doesn't resolve: the query's binding.
            "__auto_type: its own initializer's type query names it",
            "unsigned long f(void) {\n"
            "    __auto_type x =\n"
            "        sizeof(__typeof__(x));\n"
            "    return x;\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: without an initializer",
            "void f(void) {\n"
            "    __auto_type\n"
            "        x;\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: a parameter's",
            "void f(int a,\n"
            "       __auto_type b);\n",
            true,
            false
        },
        {
            "__auto_type: a function's result",
            "__auto_type\n"
            "f(void);\n",
            true,
            false
        },
        {
            "__auto_type: a typedef's",
            "typedef\n"
            "    __auto_type T;\n",
            true,
            false
        },
        {
            "__auto_type: a type name's",
            "unsigned long f(void) {\n"
            "    return sizeof(\n"
            "        __auto_type);\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: an initializer list",
            "void f(void) {\n"
            "    __auto_type x =\n"
            "        { 1 };\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: an array",
            "void f(void) {\n"
            "    __auto_type a[] =\n"
            "        { 1, 2 };\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: a bit-field's value",
            "struct B { int b : 3; };\n"
            "void f(struct B s) {\n"
            "    __auto_type x =\n"
            "        s.b;\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: a void value",
            "void f(void) {\n"
            "    __auto_type\n"
            "        x = (void)0;\n"
            "}\n",
            true,
            false
        },
        {
            // A signedness, at its keyword wherever it is among the specifiers (DeclSpec::Finish).
            "__auto_type: a signedness",
            "void f(void) {\n"
            "    long\n"
            "        unsigned __auto_type x = 1;\n"
            "}\n",
            true,
            false
        },
        {
            // What clang deduces as C++ deduces a declarator's type, and GCC refuses: refused by name.
            "__auto_type: a pointer declarator",
            "void f(void) {\n"
            "    int y = 0;\n"
            "    __auto_type *p = &y;\n"
            "}\n",
            true,
            true
        },
        {
            "__auto_type: a second declarator",
            "void f(void) {\n"
            "    __auto_type x = 1, y = 2;\n"
            "}\n",
            true,
            true
        },
        {
            // A name with linkage declared before: its types would bind before the deduction.
            "__auto_type: a linked name declared before",
            "extern int e;\n"
            "__auto_type e = 1;\n",
            true,
            true
        },
        {
            // _Alignas against the deduced type, not int, which stands for it until the initializer (#840's review): below
            // int's alignment where the deduced type's is, at file scope, a type's, and GNU's aligned, which can't lower it.
            "__auto_type: an _Alignas the deduced type allows",
            "_Alignas(2) __auto_type g = (short)1;\n"
            "void f(void) {\n"
            "    _Alignas(1) __auto_type c = (char)0;\n"
            "    _Alignas(double) __auto_type d = (char)1;\n"
            "    __auto_type e __attribute__((aligned(1))) = 1.0;\n"
            "    (void)c; (void)d; (void)e;\n"
            "}\n",
            false,
            false
        },
        {
            // Below the deduced type's alignment, though not int's: at the _Alignas.
            "__auto_type: an _Alignas below the deduced type's",
            "void f(void) {\n"
            "    _Alignas(4)\n"
            "        __auto_type x = 1.0;\n"
            "}\n",
            true,
            false
        },
        {
            "__auto_type: an _Alignas below a decayed pointer's",
            "void f(void) {\n"
            "    char a[3];\n"
            "    _Alignas(1)\n"
            "        __auto_type p = a;\n"
            "}\n",
            true,
            false
        },
        {
            // The specifiers' const and volatile on the deduced type, which _Generic of its address sees.
            "__auto_type: the specifiers' qualifiers",
            "void f(void) {\n"
            "    volatile __auto_type y = 1.0;\n"
            "    _Static_assert(_Generic(&y, volatile double *: 1, default: 0), \"volatile\");\n"
            "    const __auto_type x = 1;\n"
            "    _Static_assert(_Generic(&x, const int *: 1, default: 0), \"const\");\n"
            "}\n",
            false,
            false
        },
        {
            "__auto_type: an assignment to a const one",
            "void f(void) {\n"
            "    const __auto_type x = 1;\n"
            "    x = 2;\n"
            "}\n",
            true,
            false
        },
        // C23's `auto` (#793): an initializer's type, lvalue-converted and decayed, through a pointer declarator and
        // for each of a declaration's later declarators too, with an explicit type still the storage class.
        {
            "C23 auto: lvalue conversion, decay, pointer declarators and later declarators",
            "int g(void);\n"
            "const char *h(void) {\n"
            "    const int c = 1;\n"
            "    int a[3] = { 0 };\n"
            "    auto x = c;\n"
            "    auto p = a;\n"
            "    auto q = g;\n"
            "    auto s = \"abc\";\n"
            "    auto *t = &a[0];\n"
            "    auto **u = &t;\n"
            "    auto v = 1, w = 2;\n"
            "    auto *i = &a[0], *j = &a[1];\n"
            "    static auto k = 3;\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(x), int), \"lvalue conversion\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), \"array decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(q), int (*)(void)), \"function decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(s), char *), \"string decay\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(t), int *), \"pointer declarator\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(u), int **), \"two pointers\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(v), int), \"a later declarator\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(i), int *), \"each declarator's\");\n"
            "    x = 3;\n"
            "    p[0] = x + q() + w + (int)k + *t + **u;\n"
            "    return s;\n"
            "}\n",
            false,
            false,
            .standard = 2023
        },
        {
            "C23 auto: file scope, a loop's, a parenthesized declarator and the storage class with a type",
            "auto g = 1L;\n"
            "int f(void) {\n"
            "    int sum = 0;\n"
            "    for (auto i = 0u; i < 2; i++) sum += (int)i;\n"
            "    auto (p) = &sum;\n"
            "    auto int n = 3;\n"
            "    auto int o;\n"
            "    o = n;\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(g), long), \"file scope\");\n"
            "    _Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), \"parentheses\");\n"
            "    return *p + o;\n"
            "}\n",
            false,
            false,
            .standard = 2023
        },
        {
            "C23 auto: without an initializer",
            "void f(void) {\n"
            "    auto\n"
            "        x;\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 auto: an initializer list",
            "void f(void) {\n"
            "    auto x =\n"
            "        { 1 };\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 auto: an array",
            "void f(void) {\n"
            "    auto a[] =\n"
            "        { 1, 2 };\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 auto: two declarators of different types",
            "void f(void) {\n"
            "    auto x = 1, y = 2.0;\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        // C23's constexpr (#793): an initialized object whose value is a constant expression, const-qualified, and
        // usable where an integer constant expression is required. A pointer's initializer must be null.
        {
            "C23 constexpr: objects in constant expressions",
            "constexpr int n = 2;\n"
            "struct S { int a; };\n"
            "int main(void) {\n"
            "    constexpr int y = 1 + 1;\n"
            "    constexpr double d = 2.5;\n"
            "    constexpr int *p = 0;\n"
            "    constexpr int *q = nullptr;\n"
            "    constexpr char c = 'x';\n"
            "    constexpr auto z = 3;\n"
            "    constexpr struct S s = { 1 };\n"
            "    int a[y];\n"
            "    switch (y) { case n: break; default: break; }\n"
            "    _Static_assert(y == 2, \"an integer constant expression\");\n"
            "    _Static_assert(n == 2, \"file scope\");\n"
            "    _Static_assert(z == 3, \"with auto\");\n"
            "    return (int)sizeof a - 8 + (int)d + (p != 0) + (q != 0) + (c - 'x') + s.a - 1;\n"
            "}\n",
            false,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: not a constant expression",
            "void g(int x) {\n"
            "    constexpr int y =\n"
            "        x + 1;\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: a pointer that isn't null",
            "int x;\n"
            "constexpr int *p =\n"
            "    &x;\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: an integer not exactly representable",
            "constexpr char c =\n"
            "    300;\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: a negative value in an unsigned type",
            "constexpr unsigned u =\n"
            "    -1;\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: a floating value not exactly representable",
            "constexpr float f =\n"
            "    0.1;\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: an aggregate's initializer isn't constant",
            "struct S { int a; };\n"
            "int f(void);\n"
            "int main(void) {\n"
            "    constexpr struct S s =\n"
            "        { f() };\n"
            "    return s.a;\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: without an initializer",
            "void f(void) {\n"
            "    constexpr int\n"
            "        x;\n"
            "}\n",
            true,
            false,
            .standard = 2023
        },
        {
            "C23 constexpr: a typedef",
            "typedef constexpr int\n"
            "    T;\n",
            true,
            false,
            .standard = 2023
        },
        // A tag first named in a definition's parameter list has the body's scope too (C11 6.2.1p4), and ends with
        // it; a prototype's ends with the prototype (#435).
        {
            "definition's parameter-list tag in its body",
            "int f(struct T *p) { struct T *q = p; return q != 0; }\n",
            false,
            false
        },
        {
            "body's definition completes its parameter-list tag",
            "int f(struct T *p) { struct T { int x; }; return p->x; }\n",
            false,
            false
        },
        {
            "body's forward declaration keeps its parameter-list tag",
            "int f(struct T *p) { struct T; struct T *q = p; return q != 0; }\n",
            false,
            false
        },
        {
            "inner block's forward declaration hides a parameter-list tag",
            "int f(struct T *p) { { struct T;\n struct T *q = p; return q != 0; } }\n",
            true,
            false
        },
        {
            "parameter-list tags of a bound and a typeof in the body",
            "int f(int a[sizeof(struct V *)], typeof(struct W *) b, struct V *c) { struct V *v = c; struct W *w = b; return (v != 0) + (w != 0) + a[0]; }\n",
            false,
            false
        },
        {
            "body's prototype names its parameter-list tag",
            "int f(struct T *p) { int g(struct T *q);\n return g(p); }\n",
            false,
            false
        },
        {
            "definition's parameter-list tag ends with its body",
            "int f(struct T *p) { return p != 0; }\nstruct T *t;\nint g(void) { return f(t); }\n",
            true,
            false
        },
        {
            "prototype's parameter-list tag ends with it",
            "void g(struct S *p);\nint f(void) { struct S *q = 0; g(q); return 0; }\n",
            true,
            false
        },
        {
            "prototype's parameter-list tag isn't the file scope's",
            "void g(struct S *p);\nstruct S { int x; };\nint f(void) { struct S s = { 1 }; g(&s); return s.x; }\n",
            true,
            false
        },
        {
            "nested prototype's tag ends before the body",
            "int f(struct T *p, int (*k)(struct U *)) {\n struct U *v = 0; return k(v) + (p != 0); }\n",
            true,
            false
        },
        {
            "return type's prototype tag ends before the body",
            "int (*f(struct T *p))(struct U *u) {\n struct T *q = p;\n struct U *v = 0;\n int (*k)(struct U *) = f(q);\n return k(v); }\n",
            true,
            false
        },
        // A prototype's tag in a constant expression's type name (a typeof's, a builtin's operand) ends at the
        // prototype's `)` too (#437).
        {
            "typeof's prototype tag in a parameter list ends with the prototype",
            "int f(typeof(int (*)(struct Z *)) g, struct Z *z) { return g(z); }\n",
            true,
            false
        },
        {
            "file-scope typeof's prototype tag ends with the prototype",
            "typeof(int (*)(struct Z *)) g;\nstruct Z *z;\nint f(void) { return g(z); }\n",
            true,
            false
        },
        {
            "block-scope typeof's prototype tag ends with the prototype",
            "int f(void) {\n typeof(int (*)(struct Z *)) h = 0;\n struct Z *z = 0;\n return h(z); }\n",
            true,
            false
        },
        {
            "typeof's prototype tag in a parameter list doesn't reach the body",
            "int f(typeof(int (*)(struct Z *)) g) {\n struct Z *z = 0;\n return g(z); }\n",
            true,
            false
        },
        {
            "typeof's prototype names the visible tag",
            "struct Z;\ntypeof(int (*)(struct Z *)) g;\nstruct Z *z;\nint f(void) { return g(z); }\n",
            false,
            false
        },
        {
            "builtin operands' prototype tags are each their own",
            "_Static_assert(!__builtin_types_compatible_p(int (*)(struct Y *), int (*)(struct Y *)), \"distinct\");\n",
            false,
            false
        },
        {
            "nested prototype's tag in a typeof ends with each prototype",
            "typeof(int (*)(int (*)(struct Z *), struct Z *)) g;\nint h(struct Z *);\nint f(void) { return g(h, 0); }\n",
            true,
            false
        },
        {
            "register parameter's prototype tag ends with the prototype",
            "_Static_assert(!__builtin_types_compatible_p(int (*)(register struct S *), int (*)(struct S *)), \"distinct\");\n",
            false,
            false
        },
        // A C23 attribute's operand is read raw (#437's review): a list whose parameter starts with `register` or an
        // attribute is a prototype there too, its tag ended at its `)`.
        {
            "raw register parameter's prototype tag ends with the prototype",
            "[[gnu::aligned(sizeof(int (*)(register struct Z *)))]] int a;\nint (*g)(struct Z *);\nstruct Z *z;\nint f(void) { return g(z) + a; }\n",
            true,
            false,
            false,
            2023
        },
        {
            "raw attributed parameter's prototype tag ends with the prototype",
            "[[gnu::aligned(sizeof(int (*)(__attribute__((unused)) struct Z *)))]] int a;\nint (*g)(struct Z *);\nstruct Z *z;\nint f(void) { return g(z) + a; }\n",
            true,
            false,
            false,
            2023
        },
        {
            "raw function type of a register parameter",
            "[[gnu::aligned(sizeof(int (register struct Z *)))]] int a;\nint (*g)(struct Z *);\nstruct Z *z;\nint f(void) { return g(z) + a; }\n",
            true,
            false,
            false,
            2023
        },
        // A struct, union or enum defined in a parameter list (#438): read where it is, in source order, its tags and
        // enumeration constants in the list's scope, which is a scope of its own; a definition's are its body's too,
        // and a prototype's end with it.
        {
            "struct defined in a definition's parameter list",
            "int f(struct S { int x; } s) { return s.x; }\n",
            false,
            false
        },
        {
            "enum defined in a definition's parameter list, its constants in the body",
            "int f(enum E { K = 7 } e) { return K + (e == K); }\n",
            false,
            false
        },
        {
            "definition in a sizeof in a parameter's array bound, its tag in later parameters and the body",
            "int f(int a[sizeof(struct V { int x; })], struct V *v) { struct V *w = v; return w->x; }\n",
            false,
            false
        },
        {
            "parameter-list definitions' nested tags and constants in the body",
            "int f(union U { struct S { int x; } s; enum E { A, B } e; } u) { struct S t = u.s; enum E k = B; return t.x + (k == u.e) + A; }\n",
            false,
            false
        },
        {
            "parameter-list constant in a later parameter's bound",
            "int f(enum E { K = 3 } e, char (*p)[K + 1]) { _Static_assert(sizeof(*p) == 4, \"bound\"); return K + e; }\n",
            false,
            false
        },
        {
            "parameter-list constant shadows the file scope's in the body alone",
            "enum { K = 5 };\nint f(enum E { K = 1 } e) { _Static_assert(K == 1, \"body\"); return K + e; }\n_Static_assert(K == 5, \"file\");\n",
            false,
            false
        },
        {
            "prototype's list is a scope of its own",
            "struct S { long z; };\nenum { K };\nvoid g(struct S { int x; } s, enum E { K = 4 } e, struct S *p);\n",
            false,
            false
        },
        {
            "parameter list's redefinition of a tag only warns",
            "int f(struct S { int x; } a, struct S { int y; } b, struct S *c) { return a.x + b.y + c->x; }\n",
            false,
            false
        },
        {
            "definitions in a member's parameters and array bound",
            "struct A { void (*g)(struct B { int y; } b); int a[sizeof(struct C { int z; })]; };\nstruct C c;\nstruct B *q;\n",
            false,
            false
        },
        {
            "definitions in array bounds at file and block scope",
            "int a[sizeof(struct V { int x; })];\nstruct V v;\nint f(void) { int b[sizeof(struct W { int y; })]; struct W w = { 1 }; return w.y + (int)sizeof(b) + v.x; }\n",
            false,
            false
        },
        {
            "definitions in a type name's parameters and a variable bound",
            "int f(int n, int a[n + sizeof(struct V { int x; })]) { struct V v = { n }; return v.x + a[0] + (int)sizeof(void (*)(struct W { int y; })); }\n",
            false,
            false
        },
        {
            "enum of a fixed underlying type defined in a parameter list",
            "int f(enum E : long { K = 7 } e) { _Static_assert(sizeof(K) == sizeof(long), \"C23\"); return K == e; }\n",
            false,
            false,
            .standard = 2023
        },
        {
            "prototype's parameter-list constant ends with it",
            "void g(enum E { K = 7 } e);\nint f(void) { return K; }\n",
            true,
            false
        },
        {
            "prototype's parameter-list definition isn't the file scope's",
            "void g(struct S { int x; } s);\nstruct S { int x; } t;\nint f(void) { g(t);\n return 0; }\n",
            true,
            false
        },
        {
            "definition in a prototype's array bound ends with it",
            "void g(int a[sizeof(struct V { int x; })]);\nstruct V v;\n",
            true,
            false
        },
        {
            "nested prototype's constant ends before the body",
            "int f(int (*k)(enum E { K = 1 } e)) {\n return K; }\n",
            true,
            false
        },
        {
            "return type's prototype constant ends before the body",
            "int (*f(int x))(enum E { K } e) {\n return K + x; }\n",
            true,
            false
        },
        {
            "parameter and later enumeration constant of one list conflict",
            "int f(int K,\n enum E { K = 1 } e);\n",
            true,
            false
        },
        {
            "enumeration constant and later parameter of one list conflict",
            "int f(enum E { K = 1 } e,\n int K);\n",
            true,
            false
        },
        {
            "body's declaration conflicts with its parameter-list constant",
            "int f(enum E { K = 7 } e) {\n int K = 1; return K + e; }\n",
            true,
            false
        },
        {
            "body's definition redefines its parameter-list definition",
            "int f(struct S { int x; } s) {\n struct S { int y; } t; return t.y + s.x; }\n",
            true,
            false
        },
        // A definition in a prototype that a bound's type name holds is that prototype's, a scope a level deeper which
        // ends at its `)`, and a bound's definitions are read beside its statement expressions, before or after them
        // (#460's review).
        {
            "bound's nested prototype's constant ends before the body",
            "int f(int a[sizeof(void (*)(enum E { K = 7 }))]) {\n return K; }\n",
            true,
            false
        },
        {
            "bound's nested prototype's definition isn't the body's",
            "int g(int a[sizeof(void (*)(struct S { int x; }))]) {\n struct S s = { 1 }; return s.x; }\n",
            true,
            false
        },
        {
            "bound's nested prototype's constant doesn't conflict with a parameter",
            "int f(int K, int a[sizeof(void (*)(enum E { K = 7 }))]) { return K; }\n",
            false,
            false
        },
        {
            "bound's nested prototype's definition doesn't complete the list's tag",
            "int g(struct S *p, int a[sizeof(void (*)(struct S { int x; }))]) {\n return p->x; }\n",
            true,
            false
        },
        {
            "bound's nested prototype's definition beside the list's",
            "int g(struct S { int y; } *p, int a[sizeof(void (*)(struct S { int x; }))]) { return p->y; }\n",
            false,
            false
        },
        {
            "enumerator value's nested prototype's constant doesn't conflict",
            "enum X { K = 2, A = sizeof(void (*)(enum E { K = 1 })) };\n_Static_assert(K == 2, \"outer\");\n",
            false,
            false
        },
        {
            "file-scope bound's nested prototype's definition ends with it",
            "int a[sizeof(void (*)(struct S { int x; }))];\nstruct S s;\n",
            true,
            false
        },
        {
            "member bound's nested prototype's definition ends with it",
            "struct T { int a[sizeof(void (*)(struct S { int x; }))]; };\nstruct S s;\n",
            true,
            false
        },
        {
            "nested prototype's constant in its own later bound",
            "int f(int a[sizeof(void (*)(enum E { K = 7 }, int b[K]))]) { return 0; }\n",
            false,
            false
        },
        {
            "bound's definition before a statement expression",
            "int f(void) { int a[sizeof(struct S { int x; }) + ({ 1; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }\n",
            false,
            false
        },
        {
            "bound's definition after a statement expression",
            "int f(void) { int a[({ 1; }) + sizeof(struct S { int x; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }\n",
            false,
            false
        },
        {
            "bound's definition between statement expressions",
            "int f(int m) { int a[({ m; }) + sizeof(struct S { int x; }) + ({ m + 1; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }\n",
            false,
            false
        },
        // #126: the builtins this compiler's own main.c calls, which clang checks as these show.
        {
            "atomic read-modify-write builtins and their result types",
            "typedef unsigned long U64;\nint f(U64* u, int* i, unsigned char* c, _Bool* b, int** p, double* d)\n{\n    _Static_assert(_Generic(__atomic_fetch_add(u, 1, __ATOMIC_RELAXED), U64: 1, default: 0), \"fetch_add\");\n    _Static_assert(_Generic(__atomic_add_fetch(i, 1, __ATOMIC_SEQ_CST), int: 1, default: 0), \"add_fetch\");\n    _Static_assert(_Generic(__atomic_exchange_n(c, 1, __ATOMIC_ACQ_REL), unsigned char: 1, default: 0), \"exchange_n\");\n    _Static_assert(_Generic(__atomic_compare_exchange_n(i, i, 3, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED), _Bool: 1, default: 0), \"cas\");\n    _Static_assert(_Generic(__atomic_fetch_add(p, 4, __ATOMIC_RELAXED), int*: 1, default: 0), \"pointer add\");\n    _Static_assert(_Generic(__atomic_fetch_sub(d, 1, __ATOMIC_RELAXED), double: 1, default: 0), \"fetch_sub double\");\n    _Static_assert(_Generic(__sync_val_compare_and_swap(u, 1, 2), U64: 1, default: 0), \"sync val cas\");\n    _Static_assert(_Generic(__sync_bool_compare_and_swap(u, 1, 2), _Bool: 1, default: 0), \"sync bool cas\");\n    __sync_lock_release(b);\n    (void)__sync_fetch_and_or(u, 1, i, c);\n    (void)__atomic_fetch_nand(u, 1, __ATOMIC_RELAXED);\n    (void)__atomic_xor_fetch(b, 1, __ATOMIC_RELAXED);\n    return __sync_fetch_and_add(i, 1) + __sync_lock_test_and_set(c, 1);\n}\n",
            false,
            false
        },
        {
            "atomic min and max",
            "unsigned f(unsigned* u, int* i, float* x, _Bool* b)\n{\n    _Static_assert(_Generic(__atomic_fetch_min(u, 1, __ATOMIC_RELAXED), unsigned: 1, default: 0), \"fetch_min\");\n    _Static_assert(_Generic(__atomic_max_fetch(x, 1, __ATOMIC_RELAXED), float: 1, default: 0), \"max_fetch\");\n    return __atomic_fetch_max(i, 1, 5) + __atomic_min_fetch(u, 2, 0) + __atomic_fetch_min(b, 1, 5);\n}\n",
            false,
            false
        },
        {
            "an atomic min of a pointer refused",
            "int* f(int** p) { return __atomic_fetch_min(p, 0, 5); }\n",
            true,
            false
        },
        {
            "an atomic and of a float refused",
            "float f(float* p) { return __atomic_fetch_and(p, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic compare-exchange of a float refused",
            "_Bool f(float* p, float* e) { return __atomic_compare_exchange_n(p, e, 1, 0, 5, 2); }\n",
            true,
            false
        },
        {
            "an atomic exchange of a const object refused",
            "int f(const int* p) { return __atomic_exchange_n(p, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic read-modify-write of a _BitInt refused",
            "unsigned _BitInt(32) f(unsigned _BitInt(32)* p) { return __atomic_fetch_add(p, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic fetch with an order too few refused",
            "int f(int* p) { return __atomic_fetch_add(p, 1); }\n",
            true,
            false
        },
        // #148: the atomics clang has past the _n forms, as clang checks them.
        {
            "sized legacy atomics, the legacy minimum and maximum, and their result types",
            "int f(short* s, long* l, int* i, unsigned* u, void* v, _Bool* b)\n{\n    _Static_assert(_Generic(__sync_fetch_and_add_4(s, 1), short: 1, default: 0), \"sized by the object\");\n    _Static_assert(_Generic(__sync_val_compare_and_swap_1(l, 1, 2), long: 1, default: 0), \"val cas\");\n    _Static_assert(_Generic(__sync_bool_compare_and_swap_16(i, 1, 2), _Bool: 1, default: 0), \"bool cas\");\n    _Static_assert(_Generic(__sync_swap(l, 3), long: 1, default: 0), \"swap\");\n    _Static_assert(_Generic(__sync_fetch_and_umax(i, 1), unsigned: 1, default: 0), \"umax\");\n    __sync_lock_release_8(b);\n    return __sync_fetch_and_max(i, 5) + (int)__sync_fetch_and_umin(u, 2) + __sync_fetch_and_min(v, 3) + (int)__sync_swap_2(s, 1);\n}\n",
            false,
            false
        },
        {
            "a legacy maximum of a long refused",
            "long l;\nint f(void) { return __sync_fetch_and_max(&l, 5); }\n",
            true,
            false
        },
        {
            "a legacy minimum with a protected variable refused",
            "int i;\nint f(void) { return __sync_fetch_and_min(&i, 5, i); }\n",
            true,
            false
        },
        {
            "wrapping atomics, the byte flag and the lock-free queries",
            "struct Opaque;\nlong cell;\nenum { E = __atomic_always_lock_free(8, &cell) + __atomic_is_lock_free(2, 0) };\n_Static_assert(!__atomic_always_lock_free(3, 0), \"odd\");\nint f(unsigned char* c, _Bool* b, void* v, struct Opaque* o)\n{\n    _Static_assert(_Generic(__atomic_fetch_uinc(c, 3, __ATOMIC_SEQ_CST), unsigned char: 1, default: 0), \"uinc\");\n    _Static_assert(_Generic(__atomic_test_and_set(v, __ATOMIC_ACQUIRE), _Bool: 1, default: 0), \"tas\");\n    __atomic_clear(o, __ATOMIC_RELEASE);\n    return __atomic_fetch_udec(b, 1, __ATOMIC_RELAXED) + E + __atomic_is_lock_free(16, v);\n}\n",
            false,
            false
        },
        {
            // #932's review: a function's pointee is complete and aligned to 4 bytes, and a tag is complete where the query's
            // constant context reads it, not at the unit's end.
            "the lock-free queries of a function's pointee and of a tag completed after them",
            "struct Late;\nint late(struct Late* p) { _Static_assert(!__atomic_always_lock_free(8, p), \"incomplete here\"); return 0; }\n"
            "struct Late { long x; };\nint early(struct Late* p) { _Static_assert(__atomic_always_lock_free(8, p), \"complete here\"); return 0; }\n"
            "int function(void (*p)(void))\n{\n    _Static_assert(__atomic_always_lock_free(4, p) && __atomic_is_lock_free(4, p), \"function alignment\");\n"
            "    _Static_assert(!__atomic_always_lock_free(8, p), \"past it\");\n    return 0;\n}\n",
            false,
            false
        },
        {
            "an atomic wrapping increment of a float refused",
            "float f(float* p) { return __atomic_fetch_uinc(p, 1, 5); }\n",
            true,
            false
        },
        {
            "the byte flag of a const object refused",
            "void f(const void* p) { __atomic_clear(p, 5); }\n",
            true,
            false
        },
        {
            "generic atomics of records, arrays and long double",
            "struct S { char c[3]; };\nint f(struct S* s, int (*a)[3], long double* d)\n{\n    struct S r, v = { { 1, 2, 3 } };\n    int x[3], y[3] = { 1, 2, 3 };\n    long double e = 1.0L, n = 2.0L;\n    __atomic_load(s, &r, __ATOMIC_SEQ_CST);\n    __atomic_store(a, &y, __ATOMIC_RELEASE);\n    __atomic_exchange(s, &v, &r, __ATOMIC_ACQ_REL);\n    __atomic_load(a, &x, __ATOMIC_ACQUIRE);\n    return __atomic_compare_exchange(d, &e, &n, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) + r.c[0] + x[1];\n}\n",
            false,
            false
        },
        {
            "a generic atomic load of an _Atomic object refused",
            "_Atomic int x;\nint f(int* r) { __atomic_load(&x, r, 5); return *r; }\n",
            true,
            false
        },
        {
            "a generic atomic store's incompatible value pointer refused",
            "int x;\nvoid f(long* v) { __atomic_store(&x, v, 5); }\n",
            true,
            false
        },
        {
            "scoped, HIP and OpenCL atomics",
            "_Atomic int a;\nint f(int* p, int* e, int s)\n{\n    __scoped_atomic_thread_fence(__ATOMIC_SEQ_CST, 9);\n    __opencl_atomic_init(&a, 1);\n    return __scoped_atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST, s) + __scoped_atomic_compare_exchange_n(p, e, 2, 0, 5, 5, __MEMORY_SCOPE_DEVICE) +\n           __hip_atomic_load(p, 5, 1) + __hip_atomic_compare_exchange_strong(p, e, 3, 5, 5, 6) + __opencl_atomic_fetch_add(&a, 1, 5, 4);\n}\n",
            false,
            false
        },
        {
            "a scoped atomic's invalid scope refused",
            "int f(int* p) { return __scoped_atomic_load_n(p, 5, 6); }\n",
            true,
            false
        },
        {
            "a HIP atomic's invalid scope refused",
            "int f(int* p) { return __hip_atomic_load(p, 5, 0); }\n",
            true,
            false
        },
        {
            "an OpenCL atomic of a plain object refused",
            "int f(int* p) { return __opencl_atomic_load(p, 5, 2); }\n",
            true,
            false
        },
        {
            "a HIP atomic of an _Atomic object refused",
            "_Atomic int a;\nint f(void) { return __hip_atomic_load(&a, 5, 1); }\n",
            true,
            false
        },
        // #148: clang's atomic IEEE maximum and minimum, as clang checks them (of floating objects alone, complex ones
        // too, but not long double's).
        {
            "an atomic IEEE maximum of a _Complex float",
            "_Complex float z; int f(void){ __atomic_fetch_fmaximum(&z, 1.0f, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum of a _Float16",
            "_Float16 h; int f(void){ __atomic_fetch_fmaximum(&h, 1.0f, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE minimum of a __bf16",
            "__bf16 b; int f(void){ __atomic_fetch_fminimum(&b, 1.0f, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum_num of a __float128",
            "__float128 q; int f(void){ __atomic_fetch_fmaximum_num(&q, 1.0, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum of a long double refused",
            "long double l; int f(void){ __atomic_fetch_fmaximum(&l, 1.0, 5); return 0; }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a double by an int",
            "double d; int f(void){ return __atomic_fetch_fmaximum(&d, 1, 5); }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum with an argument too many refused",
            "float x; int f(void){ return __atomic_fetch_fmaximum(&x, 1.0, 5, 6); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a const float refused",
            "const float x; int f(void){ return __atomic_fetch_fmaximum(&x, 1.0, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE minimum_num with an order past seq_cst",
            "float x; int f(void){ return __atomic_fetch_fminimum_num(&x, 1.0, 7); }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum of an int refused",
            "int x; int f(void){ return __atomic_fetch_fmaximum(&x, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE minimum of a pointer refused",
            "int *p; int f(void){ __atomic_fetch_fminimum(&p, 0, 5); return 0; }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum_num of a _Bool refused",
            "_Bool b; int f(void){ return __atomic_fetch_fmaximum_num(&b, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a struct refused",
            "struct S { float f; } s; int f(void){ __atomic_fetch_fmaximum(&s, s, 5); return 0; }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a value, not its address, refused",
            "float x; int f(void){ return __atomic_fetch_fmaximum(x, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a float by an int",
            "float x; int f(void){ return __atomic_fetch_fmaximum(&x, 1, 5); }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum by a pointer refused",
            "float x; int f(void){ return __atomic_fetch_fmaximum(&x, &x, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE minimum_num with arguments too few refused",
            "float x; int f(void){ return __atomic_fetch_fminimum_num(&x); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of an _Atomic float refused",
            "_Atomic float x; int f(void){ return __atomic_fetch_fmaximum(&x, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE minimum of a volatile double by a float",
            "volatile double x; double f(void){ return __atomic_fetch_fminimum(&x, 2.0f, 2); }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum with its order known at run time",
            "float x; int f(int o){ return __atomic_fetch_fmaximum(&x, 1, o); }\n",
            false,
            false
        },
        {
            "a scoped atomic IEEE maximum's invalid scope refused",
            "float x; int f(void){ return __scoped_atomic_fetch_fmaximum(&x, 1, 5, 7); }\n",
            true,
            false
        },
        {
            "a scoped atomic IEEE minimum_num without its scope refused",
            "float x; int f(void){ return __scoped_atomic_fetch_fminimum_num(&x, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic minimum of a long double refused",
            "long double l; int f(void){ __atomic_fetch_min(&l, 1.0, 5); return 0; }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of an int by a float refused",
            "int x; int f(void){ return __atomic_fetch_fmaximum(&x, 1.0f, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of a _Complex long double",
            "_Complex long double z; int f(void){ __atomic_fetch_fmaximum(&z, 1.0, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE minimum_num of a _Complex double",
            "_Complex double z; int f(void){ __atomic_fetch_fminimum_num(&z, 1.0, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic minimum of a _Complex long double",
            "_Complex long double z; int f(void){ __atomic_fetch_min(&z, 1.0, 5); return 0; }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum of an over-aligned float",
            "typedef float __attribute__((aligned(16))) F; F x; int f(void){ return __atomic_fetch_fmaximum(&x, 1, 5); }\n",
            false,
            false
        },
        {
            "an atomic IEEE maximum of a vector refused",
            "typedef float V __attribute__((vector_size(16))); V v; int f(void){ __atomic_fetch_fmaximum(&v, v, 5); return 0; }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum of an enum refused",
            "enum E { A } e; int f(void){ return __atomic_fetch_fmaximum(&e, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum with two arguments too many refused",
            "float x; void f(void){ (void)__atomic_fetch_fmaximum(&x, 1, 5, 6, 7); }\n",
            true,
            false
        },
        {
            "a scoped atomic IEEE maximum_num",
            "float x; int f(void){ return __scoped_atomic_fetch_fmaximum_num(&x, 1, 5, 3); }\n",
            false,
            false
        },
        {
            "an atomic IEEE minimum of a const volatile float refused",
            "const volatile float x; int f(void){ return __atomic_fetch_fminimum(&x, 1, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE minimum by a null pointer refused",
            "float x; int f(void){ return __atomic_fetch_fminimum(&x, (void*)0, 5); }\n",
            true,
            false
        },
        {
            "an atomic IEEE maximum through a void pointer refused",
            "float x; int f(void){ return __atomic_fetch_fmaximum((void*)&x, 1, 5); }\n",
            true,
            false
        },
        {
            "128-bit operands of the overflow builtins",
            "int f(__int128 a, unsigned __int128 b, long c)\n{\n    __int128 r = 0;\n    unsigned __int128 u = 0;\n    return __builtin_mul_overflow(a, b, &r) + __builtin_add_overflow(c, a, &u) + __builtin_sub_overflow(b, c, &r);\n}\n",
            false,
            false
        },
        {
            "the rotates, unpredictable and assume",
            "unsigned long long f(unsigned char a, unsigned short b, unsigned c, unsigned long long d, long x)\n{\n    __builtin_assume(x > 0);\n    return __builtin_rotateleft8(a, 1) + __builtin_rotateright8(a, 9) + __builtin_rotateleft16(b, 3) + __builtin_rotateright16(b, 3) +\n           __builtin_rotateleft32(c, 7) + __builtin_rotateright32(c, 7) + __builtin_rotateleft64(d, 9) + __builtin_rotateright64(d, 9) +\n           (__builtin_unpredictable(x > 2) ? 1 : 0);\n}\n",
            false,
            false
        },
        {
            "a rotate of a struct refused",
            "struct S { int a; } s;\nunsigned f(void) { return __builtin_rotateleft32(s, 1); }\n",
            true,
            false
        },
        {
            "an assumption of a struct refused",
            "void f(void) { struct S { int a; } s = { 0 }; __builtin_assume(s); }\n",
            true,
            false
        },
        {
            "rdtsc, pause and xgetbv",
            "unsigned long long f(void)\n{\n    __builtin_ia32_pause();\n    return __rdtsc() + __builtin_ia32_xgetbv(0);\n}\n",
            false,
            false
        },
        {
            "the build's immediates of the x86 builtins in range",
            "typedef int I16 __attribute__((vector_size(64)));\ntypedef long long L8 __attribute__((vector_size(64)));\ntypedef long long L4 __attribute__((vector_size(32)));\ntypedef long long L2 __attribute__((vector_size(16)));\ntypedef int I8 __attribute__((vector_size(32)));\ntypedef int I4 __attribute__((vector_size(16)));\ntypedef char C16 __attribute__((vector_size(16)));\ntypedef char C32 __attribute__((vector_size(32)));\ntypedef short S32 __attribute__((vector_size(64)));\nunsigned long long f(I16 a, L8 q, L4 h, L2 t, I8 e, I4 i, C16 c, C32 w, S32 s, const void* p, void* o)\n{\n    unsigned long long r = __builtin_ia32_ucmpd512_mask(a, a, 7, 0xffff) + __builtin_ia32_ucmpd256_mask(e, e, 0, 0xff) +\n                           __builtin_ia32_ucmpd128_mask(i, i, 1, 0xff) +\n                           __builtin_ia32_ucmpb128_mask(c, c, 4, 0xffff) + __builtin_ia32_ucmpb256_mask(w, w, 4, ~0u) +\n                           __builtin_ia32_ucmpw512_mask(s, s, 6, ~0u) + __builtin_ia32_kshiftlihi(1, 255) + __builtin_ia32_kshiftlidi(1, 0);\n    I16 g = __builtin_ia32_gathersiv16si(a, p, a, 0xffff, 4) + __builtin_ia32_alignd512(a, a, 255) + __builtin_ia32_shuf_i32x4(a, a, 0) +\n            __builtin_ia32_inserti32x4(a, i, 3) + __builtin_ia32_prold512(a, 31);\n    L8 l = __builtin_ia32_gathersiv8di(q, p, e, 0xff, 8) + __builtin_ia32_gatherdiv8di(q, p, q, 0xff, 1) + __builtin_ia32_alignq512(q, q, 7);\n    __builtin_ia32_scatterdiv16si(o, 0xff, q, e, 2);\n    __builtin_ia32_scatterdiv8di(o, 0xff, q, q, 8);\n    __builtin_ia32_scattersiv16si(o, 0xffff, a, a, 4);\n    L4 x = __builtin_ia32_extracti64x4_mask(q, 1, h, 0xff) + __builtin_ia32_insert128i256(h, t, 1);\n    I4 v = __builtin_ia32_vec_set_v4si(i, 5, 3);\n    return r + (unsigned long long)g[0] + (unsigned long long)l[0] + (unsigned long long)x[0] + (unsigned long long)v[0] +\n           (unsigned)__builtin_ia32_vec_ext_v4si(i, 3) + (unsigned char)__builtin_ia32_vec_ext_v16qi(c, 15);\n}\n",
            false,
            false
        },
        {
            "a gather's scale refused",
            "typedef int I16 __attribute__((vector_size(64)));\nI16 f(I16 a, const void* p) { return __builtin_ia32_gathersiv16si(a, p, a, 0xffff, 3); }\n",
            true,
            false
        },
        {
            "a scatter's scale refused",
            "typedef long long L8 __attribute__((vector_size(64)));\ntypedef int I8 __attribute__((vector_size(32)));\nvoid f(L8 q, I8 e, void* o) { __builtin_ia32_scatterdiv16si(o, 0xff, q, e, 16); }\n",
            true,
            false
        },
        {
            "unsigned one-bit _BitInt",
            "unsigned _BitInt(1) a = 1;\n_BitInt(1) unsigned b;\ntypedef unsigned _BitInt(1) B1;\nB1 c;\nint f(void) { unsigned _BitInt(1) d = 1; return d + (int)sizeof d + (int)sizeof(unsigned _BitInt(1)); }\n",
            false,
            false
        },
        {
            "signed one-bit _BitInt refused",
            "int a;\n_BitInt(1) x;\n",
            true,
            false
        },
        {
            "a typedef of an unsigned _BitInt keeps it unsigned",
            "typedef unsigned _BitInt(8) U8;\nU8 x = 255;\n_Static_assert((U8)-1 > 0, \"unsigned\");\ntypedef _BitInt(8) S8;\n_Static_assert((S8)-1 < 0, \"signed\");\nint f(void) { U8 y = 200; return y > 100; }\n",
            false,
            false
        },
        {
            // C23's bit-precise constants (#768): their types and values, in each radix, with digit separators and in each
            // order and case of their suffixes, past 64 bits too.
            "bit-precise constants",
            "_Static_assert(_Generic(0wb, _BitInt(2): 1, default: 0) && _Generic(1wb, _BitInt(2): 1, default: 0) && _Generic(2WB, _BitInt(3): 1, default: 0), \"s\");\n"
            "_Static_assert(_Generic(0uwb, unsigned _BitInt(1): 1, default: 0) && _Generic(5Uwb, unsigned _BitInt(3): 1, default: 0), \"u\");\n"
            "_Static_assert(_Generic(5wbU, unsigned _BitInt(3): 1, default: 0) && _Generic(5WBu, unsigned _BitInt(3): 1, default: 0) && 5uWB == 5, \"o\");\n"
            "_Static_assert(_Generic(0777wb, _BitInt(10): 1, default: 0) && _Generic(0b1010'1010wb, _BitInt(9): 1, default: 0) && 0777wb == 511, \"r\");\n"
            "_Static_assert(_Generic(0x8000'0000'0000'0000wb, _BitInt(65): 1, default: 0) && _Generic(0xffffffffffffffffuwb, unsigned _BitInt(64): 1, default: 0), \"x\");\n"
            "_Static_assert(_Generic(18446744073709551616wb, _BitInt(66): 1, default: 0) && 18446744073709551616wb == (_BitInt(66))1 << 64, \"w\");\n"
            "_Static_assert(_Generic(0xffffffffffffffffffffffffffffffffuwb, unsigned _BitInt(128): 1, default: 0) &&\n"
            "               0xffffffffffffffffffffffffffffffffuwb == (unsigned _BitInt(128))-1, \"m\");\n"
            "_Static_assert(_Generic(0x7fffffffffffffffffffffffffffffffwb, _BitInt(128): 1, default: 0) && -1wb < 0 && _Generic(-1wb, _BitInt(2): 1, default: 0), \"n\");\n"
            "unsigned _BitInt(100) a = 0xf'ffff'ffff'ffff'ffff'ffff'ffffuwb;\nint f(void) { return (int)(a >> 90) + (int)0x1ffffffffffffffffuwb; }\n",
            false,
            false,
            false,
            2023
        },
        {
            "a bit-precise constant past 128 bits refused",
            "int a;\n_BitInt(129) b = 0xffffffffffffffffffffffffffffffffwb;\n",
            true,
            true,
            false,
            2023
        },
        {
            "a bit-precise suffix of mixed case",
            "int a;\n_BitInt(8) b = 5wB;\n",
            true,
            false,
            false,
            2023
        },
        {
            "no_builtin on definitions",
            "__attribute__((no_builtin(\"memset\"))) void f(void) {}\n__attribute__((no_builtin)) void g(void) {}\n__attribute__((no_builtin(\"memset\", \"memcpy\"))) void h(void) {}\n__attribute__((no_builtin(\"not_a_builtin\"))) void k(void) {}\nint x __attribute__((no_builtin(\"memset\")));\n",
            false,
            false
        },
        {
            "no_builtin on a declaration refused",
            "int a;\n__attribute__((no_builtin(\"memset\"))) void f(void);\n",
            true,
            false
        },
        {
            "no_builtin with a number refused",
            "int a;\n__attribute__((no_builtin(1))) void f(void) {}\n",
            true,
            false
        },
        {
            "FP_CONTRACT at file scope and a compound's start",
            "#pragma STDC FP_CONTRACT OFF\ndouble f(double a, double b) {\n#pragma STDC FP_CONTRACT ON\n return a * b + 1; }\n#pragma STDC FP_CONTRACT DEFAULT\ndouble g(double a) { if (a) {\n#pragma STDC FP_CONTRACT OFF\n return a; } return 0; }\n#pragma STDC FP_CONTRACT MAYBE\ndouble x;\n",
            false,
            false
        },
        {
            "FP_CONTRACT inside a compound refused",
            "double f(double a, double b) { double c = a * b;\n#pragma STDC FP_CONTRACT ON\n return c + 1; }\n",
            true,
            false
        },
        {
            "loop hints before each kind of loop",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize(disable) interleave(disable)\n for (int i = 0; i < n; i += 1) s += p[i];\n#pragma clang loop unroll(disable)\n while (n--) s += 1;\n#pragma clang loop unroll(full)\n do s += 1; while (s < 9);\n if (n)\n#pragma clang loop vectorize(assume_safety) interleave(assume_safety)\n while (n--) s += 1;\n#pragma clang loop vectorize(disable)\n#pragma clang loop interleave(disable)\n for (;;) break;\n return s; }\n",
            false,
            false
        },
        {
            "loop hint counts and widths",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize_width(4) unroll_count(8) interleave_count(2)\n while (n--) s += 1;\n#pragma clang loop vectorize_width(4, scalable) pipeline(disable)\n while (n--) s += 1;\n#pragma clang loop vectorize_width(fixed) vectorize_predicate(enable) distribute(enable) pipeline_initiation_interval(3)\n while (n--) s += 1;\n#pragma clang loop unroll_count(0x10) vectorize_predicate(enable) vectorize(disable)\n while (n--) s += 1;\n#pragma clang loop unroll(disable) vectorize(disable),\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "the unroll pragmas",
            "int f(int* p, int n) { int s = 0;\n#pragma unroll\n while (n--) s += 1;\n#pragma unroll 4\n while (n--) s += 1;\n#pragma unroll(4)\n while (n--) s += 1;\n#pragma nounroll\n while (n--) s += 1;\n#pragma unroll_and_jam 2\n while (n--) s += 1;\n#pragma nounroll_and_jam\n while (n--) s += 1;\n#pragma GCC unroll 4\n while (n--) s += 1;\n#pragma unroll 0\n while (n--) s += 1;\n#pragma nounroll 4\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "a loop hint before another statement refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize(disable)\n s = 1; return s; }\n",
            true,
            false
        },
        {
            "a loop hint before a block refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize(disable)\n { for (int i = 0; i < n; i += 1) s += p[i]; } return s; }\n",
            true,
            false
        },
        {
            "a loop hint before a labeled loop refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable)\n L: for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint after a do-while refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable)\n do s += 1; while (--n);\n#pragma clang loop unroll(disable)\n return s; }\n",
            true,
            false
        },
        {
            "a loop hint before a closing brace refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable)\n}\n",
            true,
            false
        },
        {
            "an unroll pragma before another statement refused",
            "int f(int* p, int n) { int s = 0;\n#pragma unroll\n s = 1; return s; }\n",
            true,
            false
        },
        {
            "a loop hint at file scope refused",
            "#pragma clang loop vectorize(disable)\nint x;\n",
            true,
            false
        },
        {
            "a loop hint's invalid argument refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize(maybe)\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint's invalid option refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorise(enable)\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint without an option refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint without parentheses refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize enable\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint left open refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a loop hint's zero count refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll_count(0)\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "a loop hint's count past int refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop interleave_count(2147483648)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            // #830's review: a bit-precise pack value's low 64 bits alone made 0x10000000000000001uwb an alignment of 1, and
            // one past 128 bits wrapped to its low bits. clang ignores both, with its alignment warning, and takes small ones.
            "a pack pragma's bit-precise values: small ones apply, past 64 and past 128 bits ignored",
            "#pragma pack(push, 8uwb)\nstruct A { char c; double d; };\n#pragma pack(pop)\n"
            "#pragma pack(push, 2wb)\nstruct B { char c; int i; };\n#pragma pack(pop)\n"
            "#pragma pack(4)\n#pragma pack(0x10000000000000001uwb)\nstruct C { char c; double d; };\n"
            "#pragma pack(0x1000000000000000000000000000000001uwb)\nstruct D { char c; double d; };\n#pragma pack()\n"
            "_Static_assert(sizeof(struct A) == 16 && sizeof(struct B) == 6, \"small values apply\");\n"
            "_Static_assert(sizeof(struct C) == 12 && sizeof(struct D) == 12, \"wide values leave pack(4)\");\n",
            false,
            false,
            false,
            2023,
        },
        {
            // #830's review: a bit-precise count is its value, accepted where it is an int's and refused past 64 bits and
            // past 128 bits, whatever its low bits.
            "a loop hint's bit-precise counts accepted",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count(4uwb) interleave_count(2wb)\n while (n--) s += 1; return s; }\n",
            false,
            false,
            false,
            2023,
        },
        {
            "a loop hint's bit-precise count past 64 bits refused",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count(0x10000000000000002uwb)\n while (n--) s += 1; return s; }\n",
            true,
            false,
            false,
            2023,
        },
        {
            "a loop hint's bit-precise count past 128 bits refused",
            "int f(int n) { int s = 0;\n#pragma clang loop interleave_count(0x1000000000000000000000000000000002uwb)\n while (n--) s += 1; return s; }\n",
            true,
            false,
            false,
            2023,
        },
        {
            "a loop hint's floating count refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop interleave_count(1.5)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "duplicate loop hints refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable) unroll(enable)\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "duplicate loop hints across pragmas refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop distribute(enable)\n#pragma clang loop distribute(disable)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "duplicate loop hint counts refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize_width(4, fixed) vectorize_width(8)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll pragma beside a loop hint refused",
            "int f(int* p, int n) { int s = 0;\n#pragma unroll\n#pragma clang loop unroll(disable)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "incompatible loop hints refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop vectorize(disable) vectorize_width(4)\n for (int i = 0; i < n; i += 1) s += p[i]; return s; }\n",
            true,
            false
        },
        {
            "incompatible unroll hints refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(enable) unroll_count(4)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        // #480's review: GCC unroll is clang's unroll, whose count may be left out; a count clang can't read is an error
        // (an empty one, an unclosed one, one no expression starts with), not a constant this doesn't evaluate; and the
        // pragmas clang drops (a count's tokens after nounroll, or after a count's parentheses) are no hint.
        {
            "GCC unroll without a count",
            "int g(int n)\n{\n    int s = 0;\n#pragma GCC unroll\n    for (int i = 0; i < n; i += 1)\n    {\n        s += i;\n    }\n    return s;\n}\n",
            false,
            false
        },
        {
            "an unroll count in empty parentheses refused",
            "int f(int n) { int s = 0;\n#pragma unroll()\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a GCC unroll count in empty parentheses refused",
            "int f(int n) { int s = 0;\n#pragma GCC unroll ( )\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count no expression starts with refused",
            "int f(int n) { int s = 0;\n#pragma unroll ;\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unclosed unroll_and_jam count refused",
            "int f(int n) { int s = 0;\n#pragma unroll_and_jam (4\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count's expression unsupported",
            "int f(int n) { int s = 0;\n#pragma unroll (4 + 4)\n while (n--) s += 1; return s; }\n",
            true,
            true
        },
        {
            "the unroll pragmas clang drops, and one it reads a literal from",
            "int f(int n) { int s = 0;\n#pragma nounroll 4\n#pragma unroll 4\n while (n--) s += 1;\n#pragma unroll (4) x\n#pragma unroll 8\n while (n--) s += 1;\n#pragma unroll 4)\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "an unroll count clang reads a literal from, twice, refused",
            "int f(int n) { int s = 0;\n#pragma unroll 4)\n#pragma unroll 8\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "GCC unroll beside unroll refused",
            "int f(int n) { int s = 0;\n#pragma unroll\n#pragma GCC unroll\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        // #480's second review: an empty clang loop count is clang's missing argument, and only an expression's keywords
        // start a count.
        {
            "an empty unroll_count refused",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count()\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an empty interleave_count refused",
            "int f(int n) { int s = 0;\n#pragma clang loop interleave_count()\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an empty vectorize_width refused",
            "int f(int n) { int s = 0;\n#pragma clang loop vectorize_width()\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an empty pipeline_initiation_interval refused",
            "int f(int n) { int s = 0;\n#pragma clang loop pipeline_initiation_interval()\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a clang loop count no expression starts with refused",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count(;)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a clang loop count of a statement keyword refused",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count(return)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count of a statement keyword refused",
            "int f(int n) { int s = 0;\n#pragma unroll return\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count of a type keyword refused",
            "int f(int n) { int s = 0;\n#pragma unroll int\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count of typeof refused",
            "int f(int n) { int s = 0;\n#pragma unroll typeof(1)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an unroll count of sizeof unsupported",
            "int f(int n) { int s = 0;\n#pragma unroll sizeof(int)\n while (n--) s += 1; return s; }\n",
            true,
            true
        },
        {
            "a clang loop count of an enumerator unsupported",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop unroll_count(N)\n while (n--) s += 1; return s; }\n",
            true,
            true
        },
        // clang drops a loop pragma with tokens after its options (or a count's parentheses), with a warning: no hint, so
        // nothing to place or to check, at file scope too; it ignores what follows the state or the constant expression an
        // argument starts with, with a warning; licm takes disable and vectorize_predicate assume_safety. Counts this
        // doesn't evaluate stay unsupported: their errors can precede the statement and its Sema conflicts (#503).
        {
            "loop pragmas clang drops, at file scope and before a statement",
            "#pragma clang loop unroll(enable) 4\n#pragma nounroll 4\n#pragma unroll (4) x\nint f(int n) { int s = 0;\n"
            "#pragma clang loop unroll(enable) 4\n#pragma nounroll 4\n#pragma unroll (4) x\n s += 1; return s; }\n",
            false,
            false
        },
        {
            "a dropped clang loop pragma's arguments and hints unchecked",
            "int f(int n) { int s = 0;\n#pragma clang loop unroll_count(0) 4\n#pragma clang loop unroll_count() 4\n"
            "#pragma clang loop unroll_count(4))\n#pragma clang loop unroll(disable)\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "what follows a loop hint's argument ignored",
            "int f(int n) { int s = 0;\n#pragma clang loop vectorize(enable x) unroll(enable, x)\n while (n--) s += 1;\n"
            "#pragma clang loop vectorize_width(scalable, 4) interleave_count(4, x)\n while (n--) s += 1;\n"
            "#pragma clang loop vectorize_width(4, scalable x) unroll_count(4 = 5)\n while (n--) s += 1;\n"
            "#pragma clang loop vectorize_width(4 x, scalable)\n while (n--) s += 1;\n"
            "#pragma clang loop vectorize_width(fixed, 4)\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "licm and vectorize_predicate's assume_safety",
            "int f(int n) { int s = 0;\n#pragma clang loop licm(disable) vectorize_predicate(assume_safety)\n while (n--) s += 1; return s; }\n",
            false,
            false
        },
        {
            "licm enabled refused",
            "int f(int n) { int s = 0;\n#pragma clang loop licm(enable)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "licm twice refused",
            "int f(int n) { int s = 0;\n#pragma clang loop licm(disable) licm(disable)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a vectorize_width count without its kind refused",
            "int f(int n) { int s = 0;\n#pragma clang loop vectorize_width(4, x)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "an enumerator count beside unroll(full) unsupported",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop unroll_count(N) unroll(full)\n while (n--) s += 1; return s; }\n",
            true,
            true,
            .undecided = true
        },
        {
            "an enumerator width beside vectorize(disable) unsupported",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop vectorize(disable) vectorize_width(N)\n while (n--) s += 1; return s; }\n",
            true,
            true,
            .undecided = true
        },
        {
            "an enumerator count before an invalid state refused",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop unroll_count(N) vectorize(bogus)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        // In a body, clang checks a count before it places the hint: an error at the count's pragma when it is invalid, and
        // at the statement only when it is valid (#480's fourth review).
        {
            "a misplaced enumerator count unsupported",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop unroll_count(N)\n s += 1; return s; }\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "a misplaced count of a variable unsupported",
            "int N = 4;\nvoid f(int n) {\n#pragma clang loop unroll_count(N)\n    n++;\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "a misplaced unroll count of a variable unsupported",
            "int N = 4;\nvoid f(int n) {\n#pragma unroll N\n    n++;\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "an enumerator unroll count after unroll(enable) unsupported",
            "enum { N = 4 };\nint f(int n) { int s = 0;\n#pragma clang loop unroll(enable)\n#pragma unroll N\n while (n--) s += 1; return s; }\n",
            true,
            true,
            .undecided = true
        },
        // #480's third and fourth reviews: before one loop, a later pragma's error is clang's first only when an earlier
        // count is valid, and the earlier count's when it isn't (a variable, a division by zero, an enumerator of 0). A
        // count this doesn't evaluate leaves the loop's pragmas unsupported.
        {
            "an enumerator count before unroll(full)'s pragma unsupported",
            "enum { N = 4 };\nint f(int n) {\n#pragma clang loop unroll_count(N)\n#pragma clang loop unroll(full)\n    while (n--) {}\n    return n;\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "an enumerator width before vectorize(disable)'s pragma unsupported",
            "enum { N = 4 };\nint f(int n) {\n#pragma clang loop vectorize_width(N)\n#pragma clang loop vectorize(disable)\n    while (n--) {}\n    return n;\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "an enumerator unroll count before another unsupported",
            "enum { N = 4 };\nint f(int n) {\n#pragma unroll N\n#pragma GCC diagnostic push\n#pragma unroll 4\n    while (n--) {}\n    return n;\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "a variable's count before unroll(full)'s pragma unsupported",
            "int N = 4;\nvoid f(int n) {\n#pragma clang loop unroll_count(N)\n#pragma clang loop unroll(full)\n    while (n--) {}\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "a count divided by zero before unroll(full)'s pragma unsupported",
            "void f(int n) {\n#pragma clang loop unroll_count(4 / 0)\n#pragma clang loop unroll(full)\n    while (n--) {}\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "an enumerator count of 0 before unroll(full)'s pragma unsupported",
            "enum { N = 0 };\nvoid f(int n) {\n#pragma clang loop unroll_count(N)\n#pragma clang loop unroll(full)\n    while (n--) {}\n}\n",
            true,
            true,
            false,
            0,
            false,
            true
        },
        {
            "an enumerator unroll count at file scope refused",
            "enum { N = 4 };\n#pragma unroll N\nint x;\n",
            true,
            false
        },
        // An inferred array's elements of struct type from subscripts, members and indirections, and compound literals in
        // a block-scope static's initializer, as this compiler's own main.c has them.
        {
            "struct-valued elements of an inferred array",
            "typedef struct { char* pointer; unsigned long length; } S8;\nstruct W { S8 s; S8 t[2]; };\nint f(const S8* x, struct W w, struct W** q)\n{\n    const S8 a[] = { x[1], x[2] };\n    _Static_assert(sizeof a == 2 * sizeof(S8), \"two\");\n    S8 b[] = { x[0], { \"c\", 1 }, x[1] };\n    _Static_assert(sizeof b == 3 * sizeof(S8), \"three\");\n    S8 c[] = { *x, (*x), w.s, w.t[1], q[0]->s, (*q)->t[0] };\n    _Static_assert(sizeof c == 6 * sizeof(S8), \"six\");\n    unsigned long d[] = { x[1].length, w.t[0].length, q[1]->s.length };\n    _Static_assert(sizeof d == 3 * sizeof(unsigned long), \"three scalars\");\n    return (int)(a[1].length + b[2].length + c[5].length + d[2]);\n}\n",
            false,
            false
        },
        // #495's review: a subscript's pointer as either operand, and chains of steps around 16, the walk's old limit.
        {
            "reversed subscripts in an inferred array",
            "typedef struct { int a, b; } S;\nstruct W { S s; S t[2]; };\nint f(S* p, int i, struct W* w, S** q)\n{\n    S a[] = { 0[p], 1[p] };\n    _Static_assert(sizeof a == 2 * sizeof(S), \"two\");\n    S b[] = { p[0], i[p], 1[w->t], 0[q][1], 1[0[q]], (i[q])[i] };\n    _Static_assert(sizeof b == 6 * sizeof(S), \"six\");\n    int c[] = { 0[p].a, i[p].b, (1[w->t]).a };\n    _Static_assert(sizeof c == 3 * sizeof(int), \"three scalars\");\n    return a[1].b + b[5].a + c[2];\n}\n",
            false,
            false
        },
        {
            "chains of 15, 16 and 17 steps in an inferred array",
            "typedef struct { int a, b; } S;\nstruct C { S s[2]; struct C* next; };\nint f(S*************** p, S**************** q, S***************** r, struct C* x)\n{\n    S a[] = { ***************p, ***************p };\n    _Static_assert(sizeof a == 2 * sizeof(S), \"15\");\n    S b[] = { ****************q, ****************q };\n    _Static_assert(sizeof b == 2 * sizeof(S), \"16\");\n    S c[] = { *****************r, *****************r };\n    _Static_assert(sizeof c == 2 * sizeof(S), \"17\");\n    S d[] = { x->next->next->next->next->next->next->next->next->next->next->next->next->next->next->s[1],\n              x->next->next->next->next->next->next->next->next->next->next->next->next->next->next->next->s[0], x->s[1] };\n    _Static_assert(sizeof d == 3 * sizeof(S), \"16 and 17 members and subscripts\");\n    return a[1].a + b[1].a + c[1].a + d[2].b;\n}\n",
            false,
            false
        },
        // Operators' values: pointer arithmetic under an indirection, GNU's vector operators with a cast's or a negated
        // operand, vectors past a difference and compares of pointers, which are scalars (#555's review), and scalars,
        // which brace elision takes into the vector.
        {
            "operators' values in an inferred array",
            "typedef struct { int a, b; } S;\ntypedef short v16hi __attribute__((vector_size(32)));\ntypedef long v4di __attribute__((vector_size(32)));\nint f(S* p, int n, v16hi h, v16hi k, long* l, long* m, v4di d)\n{\n    S a[] = { *(p + 1), *(1 + p), p[0], *(p - n), *(p + (l - m)), p[l == m] };\n    _Static_assert(sizeof a == 6 * sizeof(S), \"six\");\n    v16hi b[] = { h + (short)n, -h, ~k, h * k + (short)1, (short)n * h, -h + (short)n, h << 2 };\n    _Static_assert(sizeof b == 7 * sizeof(v16hi), \"seven\");\n    v16hi c[] = { (short)n + (short)1, 2 };\n    _Static_assert(sizeof c == sizeof(v16hi), \"one\");\n    v4di e[] = { (l - m) * d, d, (l == m) * d, (l < m) + d };\n    _Static_assert(sizeof e == 4 * sizeof(v4di), \"four vectors past pointers' differences and compares\");\n    return a[5].b + b[6][0] + c[0][1] + (int)e[3][0];\n}\n",
            false,
            false
        },
        // Casts' types (the reader's, at their `)`s), on either side; an operand the walk has no type for (a call through a
        // pointer's expression), which leaves a difference with a pointer open, not a pointer (#555's second review); and
        // constants, `sizeof` and `!`, scalars on a difference's right.
        {
            "casts and operands of no type in an inferred array",
            "typedef struct { int a, b; } S;\ntypedef long v4di __attribute__((vector_size(32)));\nint f(S* p, S* q, long* l, long* m, v4di d, long* (*get)(void))\n{\n    S x[] = { *(p + (p - (S*)q)), p[0], *((S*)q + (l - m)), *(p + (l - (*get)())), *((S*)l + 1), *(p - 1), *(p - sizeof(S)), *(p - !m), *(p - 'a') };\n    _Static_assert(sizeof x == 9 * sizeof(S), \"nine\");\n    v4di e[] = { (v4di)d * (l - m), (l - (long*)q) * d, d };\n    _Static_assert(sizeof e == 3 * sizeof(v4di), \"three\");\n    return x[8].a + (int)e[2][0];\n}\n",
            false,
            false
        },
        // A ?:'s value from both of its arms (#555's third review): a vector where either arm is one, a scalar arm
        // converted to it; a scalar where both are; a pointer or a record where either is, a null pointer constant or
        // GNU's `a ?: b` the other.
        {
            "a conditional's arms in an inferred array",
            "typedef struct { int a, b; } S;\ntypedef int v4si __attribute__((vector_size(16)));\nint f(int n, v4si v, v4si w, S* p, S* q, S s, S t)\n{\n    v4si a[] = { n ? v : 0, n ? 0 : v, n ? v : w, n ? (v + w) : 1, v };\n    _Static_assert(sizeof a == 5 * sizeof(v4si), \"five\");\n    v4si b[] = { n ? 1 : 2, 3 };\n    _Static_assert(sizeof b == sizeof(v4si), \"one\");\n    S x[] = { *(n ? p : 0), *(n ? 0 : q), n ? s : t, *(p ?: q), s };\n    _Static_assert(sizeof x == 5 * sizeof(S), \"five\");\n    return a[4][0] + b[0][1] + x[4].a;\n}\n",
            false,
            false
        },
        // A vector's lane, its element type's scalar (#555's fourth review): under a ?: beside a scalar, in a binary
        // operator's operand, through a member and of an operator's value.
        {
            "vector lanes in an inferred array",
            "typedef int v4si __attribute__((vector_size(16)));\ntypedef struct { v4si v; int k; } H;\nint f(int n, v4si v, H h)\n{\n    v4si a[] = { n ? v[0] : 2, 3 };\n    _Static_assert(sizeof a == sizeof(v4si), \"one\");\n    v4si b[] = { v[0] + 1, 2 };\n    _Static_assert(sizeof b == sizeof(v4si), \"one too\");\n    v4si c[] = { h.v[1], (v + v)[2], n ? 1 : v[3], 4, v };\n    _Static_assert(sizeof c == 2 * sizeof(v4si), \"four lanes, then a vector\");\n    return a[0][1] + b[0][0] + c[1][3];\n}\n",
            false,
            false
        },
        // A ?: of an extension vector's condition, element by element a vector whatever its arms (#555's fifth review):
        // the condition a vector, a vector's compare, and a lane of one beside pointer arms.
        {
            "a vector condition's ?: in an inferred array",
            "typedef int e4 __attribute__((ext_vector_type(4)));\ntypedef int g4 __attribute__((vector_size(16)));\ntypedef struct { int a, b; } S;\nint f(int n, e4 mask, g4 x, S* p, S* q)\n{\n    _Static_assert(__builtin_types_compatible_p(__typeof__(mask ? 1 : 2), e4), \"vector-valued conditional\");\n    g4 a[] = { mask ? 1 : 2, x };\n    _Static_assert(sizeof a == 2 * sizeof(g4), \"two elements\");\n    g4 d[] = { (mask < 2) ? 3 : 4, x, n ? 5 : 6, 7 };\n    _Static_assert(sizeof d == 3 * sizeof(g4), \"a compare's mask, a vector, then two scalars braced into one\");\n    S c[] = { *(n ? p : q), *(mask[0] ? p : q) };\n    _Static_assert(sizeof c == 2 * sizeof(S), \"two records\");\n    return a[1][0] + d[2][1] + c[1].a;\n}\n",
            false,
            false
        },
        // An extension vector's !, && and ||, vectors element by element, and its swizzles: one lane a scalar, more a
        // vector, an odd-length vector's halves rounded up, and swizzles of swizzles by their own lanes (#555's sixth to
        // eighth reviews).
        {
            "an extension vector's logic and swizzles in an inferred array",
            "typedef int e4 __attribute__((ext_vector_type(4)));\ntypedef int e2 __attribute__((ext_vector_type(2)));\ntypedef int g4 __attribute__((vector_size(16)));\ntypedef int e3 __attribute__((ext_vector_type(3)));\ntypedef int g2 __attribute__((vector_size(8)));\nint f(int n, e4 v, e4 w, e2 t, g4 x, e3 u, g2 z)\n{\n    _Static_assert(__builtin_types_compatible_p(__typeof__(!v), e4), \"not vector\");\n    _Static_assert(__builtin_types_compatible_p(__typeof__(v && w), e4), \"and vector\");\n    _Static_assert(__builtin_types_compatible_p(__typeof__(v || w), e4), \"or vector\");\n    g4 a[] = { !v, v && w, v || w, x };\n    _Static_assert(sizeof a == 4 * sizeof(g4), \"four\");\n    g4 b[] = { v.x, 2, 3, 4, x, v.s3, t.lo, !v.x, n && v.y };\n    _Static_assert(sizeof b == 3 * sizeof(g4), \"lanes braced into vectors\");\n    g4 c[] = { v.xyzw, v.wzyx, x, !n };\n    _Static_assert(sizeof c == 4 * sizeof(g4), \"two swizzled vectors, a vector, then a scalar\");\n    g2 d[] = { u.lo, u.hi, u.even, u.odd, z };\n    _Static_assert(sizeof d == 5 * sizeof(g2), \"odd lanes' halves round up\");\n    g4 e[] = { v.xy.lo, 2, 3, 4, x, v.xy.hi, v.zw.lo, v.xyz.lo.x, 0 };\n    _Static_assert(sizeof e == 3 * sizeof(g4), \"nested swizzles' lanes braced into vectors\");\n    g2 h[] = { v.xyzw.lo, z, v.wzyx.hi };\n    _Static_assert(sizeof h == 3 * sizeof(g2), \"nested swizzles' vectors whole\");\n    return a[3][0] + b[2][1] + c[3][0] + d[4][0] + e[2][0] + h[2][0];\n}\n",
            false,
            false
        },
        // An intrinsic's result, from its prototype, which the reader keeps at the call's `)`, whatever the last operand the
        // call names: a vector after a scalar count, and a scalar after a vector.
        {
            "intrinsics' results in an inferred array",
            "typedef short v8hi __attribute__((vector_size(16)));\ntypedef char v16qi __attribute__((vector_size(16)));\ntypedef int v4si __attribute__((vector_size(16)));\nint f(v8hi h, int n, v16qi c, v4si x)\n{\n    v8hi a[] = { __builtin_ia32_psllwi128(h, n), h, __builtin_ia32_psrawi128(h, n) };\n    _Static_assert(sizeof a == 3 * sizeof(v8hi), \"three vectors\");\n    v4si b[] = { __builtin_ia32_pmovmskb128(c), 2, 3, 4, x };\n    _Static_assert(sizeof b == 2 * sizeof(v4si), \"four scalars braced into a vector, then a vector\");\n    return a[2][0] + b[1][0];\n}\n",
            false,
            false
        },
        {
            "an intrinsic immediate cannot discard side effects",
            "typedef int V __attribute__((vector_size(64)));V f(V a,int*p){return __builtin_ia32_alignd512(a,a,((*p)++,3));}",
            true,
            false
        },
        {
            "compound literals in static initializers",
            "typedef struct { char* pointer; unsigned long length; } S8;\n#define T(text) ((S8){ .pointer = (char*)(text), .length = sizeof(text) - 1, })\nstatic const S8 file_scope[] = { T(\" \"), T(\"/**/\") };\nint f(void)\n{\n    static const S8 gaps[] = { T(\" \"), T(\"/**/\"), T(\" \\\\\\n\") };\n    _Static_assert(sizeof gaps == 3 * sizeof(S8), \"three\");\n    static const S8 one = (S8){ \"a\", 1 };\n    return (int)(gaps[1].length + file_scope[1].length + one.length);\n}\n",
            false,
            false
        },
        {
            "a compound literal of a parameter in a static's initializer refused",
            "typedef struct { char* pointer; unsigned long length; } S8;\nstruct W { S8 s; S8 t[2]; };\nint f(int n)\n{\n    static const S8 g[] = { (S8){ \"a\", n } };\n    return (int)sizeof g;\n}\n",
            true,
            false
        },
        {
            "an automatic compound literal's address in a static's initializer refused",
            "typedef struct { char* pointer; unsigned long length; } S8;\nstruct W { S8 s; S8 t[2]; };\nint f(void)\n{\n    static const S8* g = &(S8){ \"a\", 1 };\n    return g != 0;\n}\n",
            true,
            false
        },
        {
            "a struct where an inferred array's member goes refused",
            "typedef struct { char* pointer; unsigned long length; } S8;\nstruct W { S8 s; S8 t[2]; };\nint f(const S8* x)\n{\n    S8 a[] = { x->pointer, x[1] };\n    return (int)sizeof a;\n}\n",
            true,
            false
        },
        {
            "incompatible loop hints across pragmas refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll(disable)\n#pragma clang loop unroll_count(4)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "incompatible pipeline hints refused",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop pipeline(disable) pipeline_initiation_interval(3)\n while (n--) s += 1; return s; }\n",
            true,
            false
        },
        {
            "a loop hint's constant expression unsupported",
            "int f(int* p, int n) { int s = 0;\n#pragma clang loop unroll_count(4 + 4)\n while (n--) s += 1; return s; }\n",
            true,
            true
        },
        {
            "an inferred array of compound literals, calls and conditionals",
            "struct L { unsigned i; unsigned t; };\nstruct L g(int);\nunsigned f(int s, unsigned a, struct L x) {\n const struct L r[] = { s ? (struct L){ 0, a } : x, (struct L){ .i = 1, .t = a }, g(s), (x), s ? x : (struct L){ 4, a }, { 2, 3 } };\n _Static_assert(sizeof r == 6 * sizeof(struct L), \"six elements\");\n return r[0].t; }\n",
            false,
            false
        },
        {
            "an inferred array with braces elided",
            "struct L { unsigned i; unsigned t; };\nunsigned f(unsigned a) {\n const struct L r[] = { a, a + 1, 2, 3 };\n _Static_assert(sizeof r == 2 * sizeof(struct L), \"two elements\");\n return r[1].t; }\n",
            false,
            false
        },
        // Plain char unsigned (-funsigned-char): its conversions, promotions, division and shifts, character constants,
        // _Generic (char stays a type of its own), a mode attribute's base, and C23's char-based enum, and the assertions
        // that hold only where it is signed, refused.
        {
            "unsigned plain char: values",
            "_Static_assert((char)-1 == 255, \"conversion\");\n_Static_assert((char)200 > 100, \"comparison\");\n"
            "_Static_assert((int)(char)0x80 == 128, \"widening\");\n_Static_assert((char)256 == 0, \"truncation\");\n"
            "_Static_assert(-(char)1 == -1, \"promotion\");\n_Static_assert((char)-1 / 3 == 85, \"division\");\n"
            "_Static_assert(((char)0x80 >> 1) == 64, \"shift\");\n"
            "_Static_assert(_Generic((char)0, char: 1, unsigned char: 2, signed char: 3) == 1, \"distinct\");\n"
            "typedef char C2 __attribute__((mode(HI)));\n_Static_assert((C2)-1 > 0, \"mode\");\n_Static_assert(__CHAR_UNSIGNED__ == 1, \"macro\");\n",
            false,
            false,
            .char_unsigned = true
        },
        {
            "unsigned plain char: character constants",
            "_Static_assert('\\xff' == 255, \"hexadecimal\");\n_Static_assert('\\200' > 0, \"octal\");\n_Static_assert('a' == 97, \"letter\");\n"
            "_Static_assert(L'\\xff' == 255, \"wide\");\n",
            false,
            false,
            .char_unsigned = true
        },
        {
            "unsigned plain char: a char-based enum and u8 constants in C23",
            "enum E : char { A = 200 };\n_Static_assert(A == 200, \"fixed base\");\n_Static_assert((enum E)-1 > 0, \"unsigned base\");\n"
            "_Static_assert(u8'\\xff' == 255, \"u8\");\n",
            false,
            false,
            .standard = 2023,
            .char_unsigned = true
        },
        {
            "unsigned plain char: a signed conversion refused",
            "_Static_assert((char)-1 < 0, \"signed\");\n",
            true,
            false,
            .char_unsigned = true
        },
        {
            "unsigned plain char: a signed character constant refused",
            "int x = 1;\n_Static_assert('\\xff' == -1, \"signed\");\n",
            true,
            false,
            .char_unsigned = true
        },
        {
            "signed plain char: a char-based enum past its range refused in C23",
            "enum E : char { A = 200 };\n",
            true,
            false,
            .standard = 2023
        },
        {
            // A builtin type's flags don't say its signedness, which a mode attribute once read from them.
            "mode attribute: the base's signedness kept",
            "typedef unsigned int U2 __attribute__((mode(HI)));\n_Static_assert((U2)-1 > 0 && sizeof(U2) == 2, \"unsigned\");\n"
            "typedef unsigned char U4 __attribute__((mode(SI)));\n_Static_assert((U4)-1 > 0 && sizeof(U4) == 4, \"unsigned char\");\n"
            "typedef int S2 __attribute__((mode(HI)));\n_Static_assert((S2)-1 < 0, \"signed\");\n"
            "typedef char C1 __attribute__((mode(QI)));\n_Static_assert((C1)-1 < 0, \"plain char\");\n",
            false,
            false
        },
        // GNU's function multiversioning (#839): target("default") makes a function multiversioned even alone, and
        // clang refuses these declaration attributes beside it, at the function's name.
        {
            "multiversioning: always_inline beside target default",
            "int g(int x) { return x; }\n__attribute__((always_inline, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: noinline beside target default",
            "int g(int x) { return x; }\n__attribute__((noinline, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: flatten beside target default",
            "int g(int x) { return x; }\n__attribute__((flatten, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: cold beside target default",
            "int g(int x) { return x; }\n__attribute__((cold, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: hot beside target default",
            "int g(int x) { return x; }\n__attribute__((hot, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: weak beside target default",
            "int g(int x) { return x; }\n__attribute__((weak, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: const beside target default",
            "int g(int x) { return x; }\n__attribute__((const, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: pure beside target default",
            "int g(int x) { return x; }\n__attribute__((pure, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: naked beside target default",
            "int g(int x) { return x; }\n__attribute__((naked, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: alias beside target default",
            "int g(int x) { return x; }\n__attribute__((alias(\"g\"), target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: target_clones beside target",
            "int g(int x) { return x; }\n__attribute__((target_clones(\"avx2\", \"default\"), target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: cpu_specific beside target",
            "int g(int x) { return x; }\n__attribute__((cpu_specific(generic), target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
        {
            "multiversioning: used beside target default",
            "int g(int x) { return x; }\n__attribute__((used, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            false,
            false
        },
        {
            "multiversioning: nothrow beside target default",
            "int g(int x) { return x; }\n__attribute__((nothrow, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            false,
            false
        },
        {
            "multiversioning: a second target version checks the first declaration",
            "__attribute__((always_inline, target(\"avx2\"))) int h(int x);\n"
            "__attribute__((noinline, target(\"default\"))) int h(int x) { return x + 1; }\n"
            "int caller(int x) { return h(x); }\n",
            true,
            false
        },
    };
    enum
    {
        RANDOM_COUNT = 64,
        FILE_COUNT = BUSTER_ARRAY_LENGTH(fixtures) + RANDOM_COUNT,
    };
    char directory[] = "/tmp/buster-checker-XXXXXX";
    bool ready = mkdtemp(directory) != 0;
    char paths[FILE_COUNT][160];
    char* pointers[FILE_COUNT];
    ParseTestClang outcomes[FILE_COUNT];
    char random_sources[RANDOM_COUNT][512];
    CheckSourceFixture random_fixtures[RANDOM_COUNT];
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        const char* source;
        if (i < BUSTER_ARRAY_LENGTH(fixtures))
        {
            source = fixtures[i].source;
        }
        else
        {
            const uint32_t r = i - BUSTER_ARRAY_LENGTH(fixtures);
            const uint64_t a = check_test_random(test);
            const uint64_t b = check_test_random(test);
            const uint64_t c = check_test_random(test);
            const uint64_t expected = a * b + c;
            const bool mutate = (r & 1) != 0;
            snprintf(random_sources[r], sizeof(random_sources[r]),
                     "_Static_assert(((unsigned long)%" PRIu64 "u * %" PRIu64 "u + %" PRIu64 "u) == %" PRIu64 "ull, \"generated arithmetic\");\n",
                     a, b, c, expected + mutate);
            random_fixtures[r] = (CheckSourceFixture){
                .name = mutate ? "mutated constant expression" : "generated constant expression",
                .source = random_sources[r],
                .rejected = mutate
            };
            source = random_sources[r];
        }
        ready = parse_test_write(paths[i], source, strlen(source));
    }
    const char* arguments[] = {
        "-std=gnu17"
    };
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, FILE_COUNT, outcomes);
    // The fixtures in gnu23 or with plain char unsigned, again in their dialect and with their char: gnu23, gnu17 with
    // -funsigned-char, and gnu23 with it.
    static const char* const variant_arguments[3][2] = {
        { "-std=gnu23", "-fsigned-char" },
        { "-std=gnu17", "-funsigned-char" },
        { "-std=gnu23", "-funsigned-char" },
    };
    for (uint32_t v = 0; ready && v < 3; v += 1)
    {
        char* variant_pointers[FILE_COUNT];
        uint32_t variant_fixtures[FILE_COUNT];
        ParseTestClang variant_outcomes[FILE_COUNT];
        uint32_t variant_count = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            variant_pointers[variant_count] = paths[i];
            variant_fixtures[variant_count] = i;
            variant_count += (fixtures[i].standard == 2023) == (v != 1) && fixtures[i].char_unsigned == (v != 0) ? 1 : 0;
        }
        ready = variant_count == 0 || parse_test_clang(clang, variant_arguments[v], 2, variant_pointers, variant_count, variant_outcomes);
        for (uint32_t k = 0; ready && k < variant_count; k += 1)
        {
            outcomes[variant_fixtures[k]] = variant_outcomes[k];
        }
    }
    check_test_expect(test, ready, "clang source oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const CheckSourceFixture* fixture = i < BUSTER_ARRAY_LENGTH(fixtures) ? &fixtures[i] : &random_fixtures[i - BUSTER_ARRAY_LENGTH(fixtures)];
        check_test_expect(test, outcomes[i].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided) && !fixture->extension), "source fixture expectation differs from clang");
        check_test_source(test, fixture, paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < FILE_COUNT; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (directory[0] != 0)
    {
        rmdir(directory);
    }
}

// #480's fifth review: an inferred array's struct element in d parentheses, or after d ?:'s, has its type read from the
// tree's links in one pass, not from a scan of what's left at each level. Against clang at depths it parses (its
// brackets stop at 256), then alone, timed: 60,000 parentheses and 40,000 ?:'s must check within 20 times what a tenth
// of them takes, plus 0.2 s, as linear work does. The scans took 0.62 s for 30,000 parentheses on the laptop and 1.16 s
// for 30,000 ?:'s, 9 times as long as for 10,000, which a tenth's 20 times doesn't allow.
static bool check_test_nesting_source(Arena* text, uint32_t form, uint32_t depth, const char* path)
{
    const char literal[] = "(struct S){ .x = 1 }";
    const uint64_t capacity = (uint64_t)depth * 8 + 512;
    char* source = BUSTER_ARENA_PUSH_ARRAY(text, char, capacity);
    uint64_t length = 0;
    if (source != 0)
    {
        length += (uint64_t)snprintf(source, capacity, "struct S { int x; int y; };\nvoid f(struct S s) {\n    struct S a[] = { ");
        for (uint32_t level = 0; level < depth; level += 1)
        {
            memcpy(source + length, form == 0 ? "(" : "0 ? s : ", form == 0 ? 1 : 8);
            length += form == 0 ? 1 : 8;
        }
        memcpy(source + length, literal, sizeof(literal) - 1);
        length += sizeof(literal) - 1;
        for (uint32_t level = 0; form == 0 && level < depth; level += 1)
        {
            source[length++] = ')';
        }
        length += (uint64_t)snprintf(source + length, capacity - length, ", %s };\n    _Static_assert(sizeof a == 2 * sizeof(struct S), \"two elements\");\n}\n", literal);
    }
    const bool valid = source != 0 && length < capacity && parse_test_write(path, source, length);
    return valid;
}

static __attribute__((noinline)) void check_test_initializer_nesting(CheckTest* test, const char* clang)
{
    static const uint32_t compared[] = {
        64,
        200,
    };
    static const uint32_t timed[] = {
        60000,
        40000,
    };
    char directory[] = "/tmp/buster-checker-nesting-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    check_test_expect(test, created, "initializer nesting fixtures' directory");
    for (uint32_t form = 0; created && form < 2; form += 1)
    {
        const char* name = form == 0 ? "a struct element in nested parentheses" : "a struct element after nested ?:'s";
        const char* levels = form == 0 ? "parentheses" : "?:'s";
        // Depths clang parses, against its answer, and then a tenth of the timed depth and the timed depth itself.
        double seconds[2] = { 0 };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(compared) + 2; c += 1)
        {
            const bool timing = c >= BUSTER_ARRAY_LENGTH(compared);
            const uint32_t depth = !timing ? compared[c] : c == BUSTER_ARRAY_LENGTH(compared) ? timed[form] / 10 : timed[form];
            char path[96];
            snprintf(path, sizeof(path), "%s/%u.c", directory, form * 8 + c);
            char* paths[] = {
                path,
            };
            const char* arguments[] = {
                "-std=gnu17",
            };
            Arena text = arena_reserve((uint64_t)depth * 8 + 4096);
            ParseTestClang outcome = { 0 };
            bool ready = check_test_nesting_source(&text, form, depth, path);
            ready = ready && (timing || parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome));
            char description[200];
            snprintf(description, sizeof(description), "%u %s around an inferred array's struct element: %s", depth, levels,
                     timing ? "the fixture is written" : "clang accepts it");
            check_test_expect(test, ready && !outcome.rejected, description);
            if (ready)
            {
                const CheckSourceFixture fixture = {
                    .name = name,
                    .source = (const char*)text.base,
                };
                struct timespec start;
                struct timespec stop;
                clock_gettime(CLOCK_MONOTONIC, &start);
                check_test_source(test, &fixture, path, &outcome);
                clock_gettime(CLOCK_MONOTONIC, &stop);
                seconds[timing ? c - BUSTER_ARRAY_LENGTH(compared) : 0] = timing ? (double)(stop.tv_sec - start.tv_sec) + (double)(stop.tv_nsec - start.tv_nsec) * 1e-9 : 0;
            }
            unlink(path);
            arena_release(&text);
        }
        char description[200];
        snprintf(description, sizeof(description), "%u %s checked in %.3f s, within 20 times %u's %.3f s and 0.2 s", timed[form], levels, seconds[1],
                 timed[form] / 10, seconds[0]);
        check_test_expect(test, seconds[1] < 20.0 * seconds[0] + 0.2, description);
    }
    if (created)
    {
        rmdir(directory);
    }
}

// #591: member accesses checked in time linear in the unit, whatever the type table's size and the records'. The lookup
// cleared a byte per type at every access, and compared the record's names one at a time. Two units, each against clang
// at a small size, then timed at a tenth of a size and at the size, both paths (check_test_source, whose typed streams,
// the members' offsets among them, must agree): the larger must check within 20 times what the tenth takes, plus 0.2 s,
// as linear work does.
//   - `size` structs of 3 to 26 members, every 7th with an anonymous union too, each with a pointer to it of 50 levels,
//     whose types fill the table, and a function that reads 16 of its members;
//   - one struct of `size` members, an anonymous struct of two every 50th, and a function that reads every name.
// Before #591, on the laptop, the second took 52 times its tenth's time (1.209 s), and failed; the first, 11.5 times: its
// bytes cleared grew as the square of its size, but stayed cheap beside the rest of its work at a size a test can
// afford (#591's measurements have larger units).
static bool check_test_member_source(Arena* text, uint32_t form, uint32_t size, const char* path)
{
    const uint64_t capacity = (uint64_t)size * 640 + 4096;
    char* source = BUSTER_ARENA_PUSH_ARRAY(text, char, capacity);
    uint64_t length = 0;
    for (uint32_t i = 0; source != 0 && form == 0 && i < size && length + 640 < capacity; i += 1)
    {
        // a, b, c, then d0 to d22, and u and v.
        const uint32_t extra = i % 24;
        const bool anonymous = i % 7 == 0;
        length += (uint64_t)snprintf(source + length, capacity - length, "struct s%u { int a; int b; long c;", i);
        for (uint32_t d = 0; d < extra; d += 1)
        {
            length += (uint64_t)snprintf(source + length, capacity - length, " int d%u;", d);
        }
        length += (uint64_t)snprintf(source + length, capacity - length, "%s };\nstruct s%u ", anonymous ? " union { int u; short v; };" : "", i);
        memset(source + length, '*', 50);
        length += 50;
        length += (uint64_t)snprintf(source + length, capacity - length, " q%u;\nlong f%u(struct s%u* p) { return 0", i, i, i);
        const uint32_t names = 3 + extra + (anonymous ? 2 : 0);
        for (uint32_t read = 0; read < 16; read += 1)
        {
            const uint32_t name = (read * 5 + i) % names;
            if (name < 3)
            {
                length += (uint64_t)snprintf(source + length, capacity - length, " + p->%c", 'a' + name);
            }
            else if (name < 3 + extra)
            {
                length += (uint64_t)snprintf(source + length, capacity - length, " + p->d%u", name - 3);
            }
            else
            {
                length += (uint64_t)snprintf(source + length, capacity - length, " + p->%c", name == 3 + extra ? 'u' : 'v');
            }
        }
        length += (uint64_t)snprintf(source + length, capacity - length, "; }\n");
    }
    if (source != 0 && form == 1)
    {
        length += (uint64_t)snprintf(source + length, capacity - length, "struct big {\n");
        for (uint32_t i = 0; i < size && length + 640 < capacity; i += 1)
        {
            length += (uint64_t)(i % 50 == 49 ? snprintf(source + length, capacity - length, "    struct { int x%u; short y%u; };\n", i, i) :
                                               snprintf(source + length, capacity - length, "    int m%u;\n", i));
        }
        length += (uint64_t)snprintf(source + length, capacity - length, "};\nlong f(struct big* p)\n{\n    long s = 0;\n");
        for (uint32_t i = 0; i < size && length + 640 < capacity; i += 1)
        {
            length += (uint64_t)(i % 50 == 49 ? snprintf(source + length, capacity - length, "    s += p->x%u + p->y%u;\n", i, i) :
                                               snprintf(source + length, capacity - length, "    s += p->m%u;\n", i));
        }
        length += (uint64_t)snprintf(source + length, capacity - length, "    return s;\n}\n");
    }
    const bool valid = source != 0 && length + 640 < capacity && parse_test_write(path, source, length);
    if (valid)
    {
        source[length] = 0;
    }
    return valid;
}

static __attribute__((noinline)) void check_test_member_scaling(CheckTest* test, const char* clang)
{
    static const uint32_t timed[] = {
        2000,
        30000,
    };
    char directory[] = "/tmp/buster-checker-members-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    check_test_expect(test, created, "member scaling fixtures' directory");
    for (uint32_t form = 0; created && form < 2; form += 1)
    {
        const char* name = form == 0 ? "many records' members after a large type table" : "a record of many members";
        // 100 against clang, then a tenth of the timed size and the timed size itself.
        double seconds[2] = { 0 };
        for (uint32_t c = 0; c < 3; c += 1)
        {
            const bool timing = c != 0;
            const uint32_t size = c == 0 ? 100 : c == 1 ? timed[form] / 10 : timed[form];
            char path[96];
            snprintf(path, sizeof(path), "%s/%u.c", directory, form * 8 + c);
            char* paths[] = {
                path,
            };
            const char* arguments[] = {
                "-std=gnu17",
            };
            Arena text = arena_reserve((uint64_t)size * 640 + 4096);
            ParseTestClang outcome = { 0 };
            bool ready = check_test_member_source(&text, form, size, path);
            ready = ready && (timing || parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &outcome));
            char description[200];
            snprintf(description, sizeof(description), "%s, %u of them: %s", name, size, timing ? "the fixture is written" : "clang accepts it");
            check_test_expect(test, ready && !outcome.rejected, description);
            if (ready)
            {
                // Room for the unit's types: the records', their pointers' 50 levels and their functions'.
                const TypeCapacity capacity = {
                    .types = size * 64 + 4096,
                    .parameters = size * 2 + 4096,
                    .tags = size * 2 + 512,
                    .members = size * 32 + 4096,
                };
                const CheckSourceFixture fixture = {
                    .name = name,
                    .source = (const char*)text.base,
                    .capacity = &capacity,
                    .check_seconds = timing ? &seconds[c - 1] : NULL,
                };
                check_test_source(test, &fixture, path, &outcome);
            }
            unlink(path);
            arena_release(&text);
        }
        char description[200];
        snprintf(description, sizeof(description), "%s: %u read and checked in %.3f s, within 20 times %u's %.3f s and 0.2 s", name, timed[form], seconds[1],
                 timed[form] / 10, seconds[0]);
        check_test_expect(test, seconds[1] < 20.0 * seconds[0] + 0.2, description);
        printf("test-checker: %s\n", description);
    }
    if (created)
    {
        rmdir(directory);
    }
}

static __attribute__((noinline)) void check_test_type_capacity(CheckTest* test)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    const ParseOptions parser_options = { 0 };
    const TypeCapacity capacity = {
        .types = 0,
        .parameters = 8,
        .tags = 4,
        .members = 4,
    };
    Arena type_storage = arena_reserve(type_capacity_bytes(capacity));
    Arena type_index = arena_reserve(type_index_capacity_bytes(capacity));
    Arena storage = arena_reserve(1u << 24);
    Arena output = arena_reserve(1u << 20);
    Arena scratch = arena_reserve(1u << 24);
    TypeTable table = { 0 };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                 parse_initialize(&parser, &parser_options, pp_options.language) &&
                 type_initialize(&table, &type_storage, &type_index, capacity);
    static const char source[] = "int *p;\n";
    ready = ready && pp_run_source(&pp, "checker-capacity.c", (const uint8_t*)source, sizeof(source) - 1) && parse_translation_unit(&parser, &pp);
    check_test_expect(test, ready, "type-capacity source initialization");
    if (ready)
    {
        CheckInput input;
        pp.options.predefines.short_wchar = true;
        const bool target = check_input(&input, &parser, &pp, &table, &storage, &scratch);
        check_test_expect(test, !target && !input.resource && input.graph.count == 0 && input.report_count == 1 && input.reports[0].message == CHECK_INPUT_TARGET,
                          "source adapter rejects a mismatched target model before type construction");
        pp.options.predefines.short_wchar = false;
        const bool adapted = check_input(&input, &parser, &pp, &table, &storage, &scratch);
        bool resource_report = false;
        for (uint32_t i = 0; i < input.report_count; i += 1)
        {
            resource_report |= input.reports[i].message == CHECK_INPUT_RESOURCE;
        }
        check_test_expect(test, !adapted && input.resource && resource_report, "adapter preserves derived-type capacity failure");
        CheckGraph graph;
        const bool initialized = check_graph_initialize(&graph, &storage, 2, 0);
        check_test_expect(test, initialized, "type-capacity graph initialization");
        if (initialized)
        {
            const CheckValue lvalue = {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
            };
            const uint32_t leaf = check_test_leaf(&graph, 0, lvalue);
            check_graph_add(&graph, CHECK_OP_ADDRESS, 1, leaf, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
            const uint64_t output_mark = output.length;
            const uint64_t scratch_mark = scratch.length;
            const CheckOptions options = { 0 };
            CheckStream stream;
            const bool checked = check_graph_run(&table, &graph, &output, &scratch, &options, &stream);
            check_test_expect(test, !checked && stream.status == CHECK_STATUS_RESOURCE && stream.failed_node == 1 && stream.count == 0 &&
                                   stream.values == 0 && output.length == output_mark && scratch.length == scratch_mark,
                              "scheduler exposes type-capacity failure and rolls back output");
        }
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    arena_release(&type_index);
    arena_release(&type_storage);
}

static __attribute__((noinline)) void check_test_target_options(CheckTest* test)
{
    PPOptions options = { 0 };
    check_test_expect(test, check_input_target_supported(&options), "default semantic target is supported");
    options.predefines.char_unsigned = true;
    check_test_expect(test, check_input_target_supported(&options), "unsigned plain-char target is supported");
    options.predefines.char_unsigned = false;
    options.predefines.short_wchar = true;
    check_test_expect(test, !check_input_target_supported(&options), "short wchar target is explicit unsupported input");
    options.predefines.short_wchar = false;
    options.predefines.long_double = BUSTER_PREDEFINES_LONG_DOUBLE_64;
    check_test_expect(test, !check_input_target_supported(&options), "64-bit long double target is explicit unsupported input");
    options.predefines.long_double = BUSTER_PREDEFINES_LONG_DOUBLE_128;
    check_test_expect(test, !check_input_target_supported(&options), "128-bit long double target is explicit unsupported input");
    options.predefines.long_double = BUSTER_PREDEFINES_LONG_DOUBLE_80;
    options.predefines.fp_eval_method = BUSTER_PREDEFINES_EVAL_SOURCE;
    check_test_expect(test, check_input_target_supported(&options), "explicit source floating evaluation is supported");
    options.predefines.fp_eval_method = BUSTER_PREDEFINES_EVAL_DOUBLE;
    check_test_expect(test, !check_input_target_supported(&options), "double floating evaluation model is explicit unsupported input");
    options.predefines.fp_eval_method = BUSTER_PREDEFINES_EVAL_EXTENDED;
    check_test_expect(test, !check_input_target_supported(&options), "extended floating evaluation model is explicit unsupported input");
    options.predefines.fp_eval_method = BUSTER_PREDEFINES_EVAL_DEFAULT;
    uint16_t no_sse = (uint16_t)(predefines_feature("sse") << 1);
    options.predefines.features = &no_sse;
    options.predefines.feature_count = 1;
    options.predefines.feature_capacity = 1;
    check_test_expect(test, !check_input_target_supported(&options), "implicit extended evaluation without SSE is rejected");
    options.predefines.fp_eval_method = BUSTER_PREDEFINES_EVAL_SOURCE;
    check_test_expect(test, check_input_target_supported(&options), "explicit source evaluation controls the semantic model");
    options.assembly = true;
    check_test_expect(test, !check_input_target_supported(&options), "assembly input is outside the C semantic adapter");
}

#include "c_check_graph_test.c"
#include "c_check_builtin_graph_test.c"
#include "c_check_varargs_graph_test.c"
#include "c_check_special_graph_test.c"
#include "c_check_builtin_vector_graph_test.c"
#include "c_check_x86_graph_test.c"
#include "c_check_binding_test.c"
#include "c_check_body_bindings_test.c"
#include "c_check_environment_test.c"
#include "c_check_source_test.c"
#include "c_check_record_input_test.c"
#include "c_check_overload_test.c"
#include "c_check_overload_address_test.c"
#include "c_types_function_abi_test.c"
#include "c_check_constant_test.c"
#include "c_check_generic_test.c"
#include "c_check_enum_test.c"
#include "c_check_vla_test.c"
#include "c_check_data_test.c"
#include "c_check_actions_extra_test.c"
#include "c_check_transparent_test.c"
#include "c_check_transparent_type_test.c"
#include "c_check_complex_test.c"
#include "c_check_integer_extension_test.c"
#include "c_check_sizeof_test.c"
#include "c_check_integer_policy_test.c"
#include "c_check_fp_actions_test.c"
#include "c_check_query_runtime_test.c"
#include "c_check_vector_test.c"
#include "c_check_vector_graph_test.c"
#include "c_check_function_name_test.c"
#include "c_check_pragma_test.c"
#include "c_check_diagnostic_test.c"
#include "c_check_warning_events_test.c"
#include "c_check_const_values_test.c"
#include "c_check_warning_deferred_test.c"
#include "c_check_warning_policy_test.c"
#include "c_check_warning_render_test.c"
#include "c_check_warning_reader_test.c"
#include "c_check_literal_warning_test.c"
#include "c_check_warning_graph_test.c"
#include "c_check_warning_initializer_test.c"
#include "c_check_warning_undefined_test.c"
#include "c_check_lazy_test.c"
#include "c_check_intrinsic_input_test.c"
#include "c_check_initializer_test.c"
#include "c_check_data_source_test.c"
#include "c_check_attribute_test.c"
#include "c_check_program_test.c"
#include "c_check_typeof_compound_test.c"
#include "c_check_cleanup_test.c"
#include "c_check_musttail_test.c"
#include "c_check_program_bits_test.c"
#include "c_check_program_align_test.c"
#include "c_check_const_source_test.c"
#include "c_check_const_alias_test.c"
#include "c_check_comma_input_test.c"
#include "c_check_embed_test.c"
#include "c_check_prefix_test.c"
#include "c_check_repair_test.c"
#include "c_check_input_scan_test.c"
#include "c_check_warning_input_test.c"
#include "c_check_selection_test.c"
#include "c_check_program_library_test.c"
#include "c_check_program_classify_test.c"

// The reader's count of builtin call records (check_input_builtin_capacity), by both paths, against the test's own, on
// builtins columns of every length to 200 that end at a PROT_NONE page: mostly tokens without a builtin, and builtins
// that take a record and that don't, at random places.
static __attribute__((noinline)) void check_test_builtin_capacity(CheckTest* test)
{
    static const uint16_t recorded[] = {
        CBUILTIN_HUGE_VAL,
        CBUILTIN_FLT_ROUNDS,
        CBUILTIN_CONSTANT_P,
        CBUILTIN_OBJECT_SIZE,
        CBUILTIN_ATOMIC_LOAD_N,
        CBUILTIN_ATOMIC_FETCH_ADD,
        CBUILTIN_ATOMIC_MAX_FETCH,
        CBUILTIN_SYNC_LOCK_RELEASE,
        CBUILTIN_SHUFFLEVECTOR,
    };
    static const uint16_t unrecorded[] = {
        CBUILTIN_BSWAP16,
        CBUILTIN_ADD_OVERFLOW,
        CBUILTIN_UMULL_OVERFLOW,
        CBUILTIN_EXPECT,
        CBUILTIN_MEMCPY,
        CBUILTIN_BIT_CAST,
        CBUILTIN_ELEMENTWISE_ABS,
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "guarded builtin column allocation");
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint16_t* builtins = (uint16_t*)(mapping + page) - count;
        uint32_t expected = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t draw = check_test_random(test) % 16;
            const uint32_t pick = check_test_random(test);
            builtins[i] = draw < 2 ? recorded[pick % BUSTER_ARRAY_LENGTH(recorded)] : draw < 4 ? unrecorded[pick % BUSTER_ARRAY_LENGTH(unrecorded)] : 0;
            expected += draw < 2;
        }
        const uint32_t vector = check_input_builtin_capacity(builtins, count, false);
        const uint32_t scalar = check_input_builtin_capacity(builtins, count, true);
        char description[160];
        snprintf(description, sizeof(description), "builtin call records of %u tokens: %u by the vector path and %u by the scalar one, for %u", count,
                 vector, scalar, expected);
        check_test_expect(test, vector == expected && scalar == expected, description);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
}

static void check_test_source_benchmark(CheckTest* test);
static void check_test_lowering_source_benchmark(CheckTest* test);
static void check_test_backend_source_benchmark(CheckTest* test);
static void check_test_corpus_symbol_reasons(CheckTest* test, const char* clang);
static void check_test_mutations(CheckTest* test, const char* clang);
static void check_test_constraints(CheckTest* test, const char* clang);
static void check_test_x86_diagnostics(CheckTest* test, const char* clang);

static int run_checker_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
    CheckTest test = {
        .random = 133
    };
    const char* clang = "clang";
    bool program_initializers = false;
    bool const_values = false;
    bool program_vectors = false;
    bool program_amx = false;
    bool program_asm = false;
    bool program_bits = false;
    bool program_align = false;
    bool program_overloads = false;
    bool program_library = false;
    bool embed_initializers = false;
    bool sizeof_only = false;
    bool options = true;
    for (int i = 0; options && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const char* value = argv[++i];
            test.random = strtoull(value, &end, 10);
            options = errno == 0 && value != end && *end == 0 && *value != '-';
        }
        else if (strcmp(argv[i], "--program-initializers") == 0)
        {
            program_initializers = true;
        }
        else if (strcmp(argv[i], "--const-values") == 0)
        {
            const_values = true;
        }
        else if (strcmp(argv[i], "--program-overloads") == 0)
        {
            program_overloads = true;
        }
        else if (strcmp(argv[i], "--program-library") == 0)
        {
            program_library = true;
        }
        else if (strcmp(argv[i], "--program-vectors") == 0)
        {
            program_vectors = true;
        }
        else if (strcmp(argv[i], "--program-amx") == 0)
        {
            program_amx = true;
        }
        else if (strcmp(argv[i], "--program-asm") == 0)
        {
            program_asm = true;
        }
        else if (strcmp(argv[i], "--program-bits") == 0)
        {
            program_bits = true;
        }
        else if (strcmp(argv[i], "--program-align") == 0)
        {
            program_align = true;
        }
        else if (strcmp(argv[i], "--embed-initializers") == 0)
        {
            embed_initializers = true;
        }
        else if (strcmp(argv[i], "--sizeof") == 0)
        {
            sizeof_only = true;
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else
        {
            options = false;
        }
    }
    CheckTestTypes types = { 0 };
    check_test_expect(&test, options, "usage: test-checker [--seed N] [--clang PATH] [--program-initializers|--program-vectors|--program-amx|--program-asm|--program-bits|--program-align|--program-overloads|--program-library|--embed-initializers|--const-values|--sizeof]");
    // The process's oracle cache, set up before any check runs clang through it (oracle_cache.c, #542).
    OracleCache* oracle = oracle_cache();
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_test_expect(&test, initialized, "type-table allocation");
        if (initialized && sizeof_only)
        {
            check_test_sizeof_scaling(&test);
            check_test_integer_extensions(&test, clang);
            check_test_vla_sources(&test, clang);
        }
        else if (initialized && const_values)
        {
            check_test_const_values(&test);
            check_test_const_sources(&test, clang);
            check_test_const_alias_sources(&test, clang);
            check_test_const_programs(&test, clang);
        }
        else if (initialized && embed_initializers)
        {
            check_test_embed_initializers(&test, clang);
        }
        else if (initialized && program_initializers)
        {
            check_test_program_initializers(&test, clang);
        }
        else if (initialized && program_overloads)
        {
            check_test_program_overloads(&test, clang);
            check_test_data_sources(&test);
        }
        else if (initialized && program_library)
        {
            check_test_library(&test, clang);
        }
        else if (initialized && program_amx)
        {
            check_test_program_amx(&test, clang);
        }
        else if (initialized && program_vectors)
        {
            check_test_program_vectors(&test, clang);
            check_test_program_x86(&test, clang);
            check_test_program_x86_rest(&test, clang);
            check_test_program_vector_builtins(&test, clang);
            check_test_program_fp16(&test, clang);
            check_test_program_bf16(&test, clang);
            check_test_program_halves(&test, clang);
        }
        else if (initialized && program_asm)
        {
            check_test_program_asm(&test, clang);
        }
        else if (initialized && program_bits)
        {
            check_test_bits(&test, clang);
        }
        else if (initialized && program_align)
        {
            check_test_align(&test, clang);
        }
        else if (initialized)
        {
            check_test_action_fixtures(&test, &types.table);
            check_test_action_constraints(&test, &types.table);
            check_test_action_regressions(&test, &types.table);
            check_test_update_actions(&test, &types.table);
            check_test_integer_float_rounding(&test, &types.table);
            check_test_action_oracles(&test, clang);
            check_test_actions_batch(&test, &types.table);
            check_test_warning_events(&test);
            check_test_const_values(&test);
            check_test_warning_deferred(&test);
            check_test_warning_set(&test);
            check_test_warning_policy(&test);
            check_test_warning_render(&test);
            check_test_warning_reader(&test);
            check_test_warning_graph(&test);
            check_test_warning_initializers(&test);
            check_test_warning_undefined(&test);
            check_test_const_sources(&test, clang);
            check_test_const_alias_sources(&test, clang);
            check_test_const_programs(&test, clang);
            Arena storage = arena_reserve(UINT64_C(1) << 27);
            Arena output = arena_reserve(UINT64_C(1) << 28);
            Arena scratch = arena_reserve(UINT64_C(1) << 27);
            check_test_literals(&test, &scratch);
            check_test_literal_warnings(&test, &scratch);
            check_test_graph_fixtures(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_random(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_recorded(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_limits(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_type_order(&test, &storage, &output, &scratch);
            check_test_graph_arguments(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_guarded(&test, &types.table, &output, &scratch);
            check_test_graph_regressions(&test, &types.table, &storage, &output, &scratch);
            arena_release(&scratch);
            arena_release(&output);
            arena_release(&storage);
            check_test_sources(&test, clang);
            check_test_typeof_compounds(&test, clang);
            check_test_constraints(&test, clang);
            check_test_initializer_nesting(&test, clang);
            check_test_member_scaling(&test, clang);
            check_test_source_extensions(&test, clang);
            check_test_record_names(&test);
            check_test_overloads(&test, clang);
            check_test_overload_sources(&test, clang);
            check_test_overload_addresses(&test, clang);
            check_test_function_abis(&test, clang);
            check_test_constant_sources(&test, clang);
            check_test_generic_index(&test);
            check_test_generic_sources(&test, clang);
            check_test_enum_sources(&test, clang);
            check_test_vla_sources(&test, clang);
            check_test_type_capacity(&test);
            check_test_target_options(&test);
            check_test_bindings(&test);
            check_test_builtin_graph(&test, &types.table, clang);
            check_test_builtin_capacity(&test);
            check_test_input_scans(&test);
            check_test_expression_commas(&test);
            check_test_warning_ranges(&test);
            check_test_selection_roots(&test);
            check_test_varargs_graph(&test, &types.table);
            check_test_special_graph(&test, &types.table, clang);
            check_test_builtin_vector_graph(&test, &types.table, clang);
            check_test_x86_graph(&test, &types.table, clang);
            check_test_environments(&test);
            check_test_body_binding_guards(&test);
            check_test_body_binding_exit_points(&test);
            check_test_data(&test, &types.table, clang);
            check_test_actions_extra(&test, &types.table, clang);
            check_test_transparent_calls(&test, &types.table, clang);
            check_test_transparent_types(&test, clang);
            check_test_complex(&test, &types.table, clang);
            check_test_integer_extensions(&test, clang);
            check_test_sizeof_scaling(&test);
            check_test_integer_policy(&test, &types.table, clang);
            check_test_fp_actions(&test, &types.table);
            check_test_query_runtime(&test, &types.table, clang);
            check_test_vectors(&test, &types.table, clang);
            check_test_vector_graph(&test, &types.table);
            check_test_function_names(&test, clang);
            check_test_pragmas(&test, clang);
            check_test_diagnostic_policy(&test);
            check_test_lazy(&test);
            check_test_intrinsic_inputs(&test, clang);
            check_test_x86_diagnostics(&test, clang);
            check_test_initializers(&test, clang);
            check_test_embed_initializers(&test, clang);
            check_test_data_sources(&test);
            check_test_attributes(&test, clang);
            check_test_attribute_sources(&test, clang);
            check_test_programs(&test, clang);
            check_test_cleanups(&test, clang);
            check_test_musttails(&test, clang);
            check_test_bits(&test, clang);
            check_test_align(&test, clang);
            check_test_program_initializers(&test, clang);
            check_test_library(&test, clang);
            check_test_classify(&test, clang);
            check_test_prefixes(&test, clang);
            check_test_repairs(&test, clang);
            check_test_mutations(&test, clang);
            check_test_source_benchmark(&test);
            check_test_lowering_source_benchmark(&test);
            check_test_backend_source_benchmark(&test);
            check_test_corpus_symbol_reasons(&test, clang);
        }
        // The program harness's build directory, which the sections' programs share: removed after the last of them,
        // whichever sections ran.
        check_test_program_build_release();
        check_test_types_close(&types);
    }
    oracle_cache_report(oracle, "test-checker");
    printf("test-checker: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int returned = test.failures != 0;
    oracle_epoch_end(epoch);
    return returned;
}
