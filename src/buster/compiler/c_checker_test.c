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

static bool check_test_types_open(CheckTestTypes* types)
{
    const TypeCapacity capacity = {
        .types = 4096,
        .parameters = 8192,
        .tags = 512,
        .members = 4096,
    };
    types->arena = arena_reserve(type_capacity_bytes(capacity));
    types->index = arena_reserve(type_index_capacity_bytes(capacity));
    return type_initialize(&types->table, &types->arena, &types->index, capacity);
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
    check_test_expect(test, result.value.fault == CHECK_FAULT_FLOAT_RANGE && result.value.fault_node == 93, "out-of-range floating conversion is a deferred fault");
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
        const CheckLiteral value = check_literal_character((const uint8_t*)characters[i].spelling, (uint32_t)strlen(characters[i].spelling), false);
        check_test_expect(test, value.valid == characters[i].valid && (!value.valid || (value.type == characters[i].type && value.integer == characters[i].integer)),
                          "bounded character decoding matches the expected target value");
    }
    const uint8_t embedded_zero[] = {
        '\'',
        '\\',
        0,
        '\''
    };
    check_test_expect(test, !check_literal_character(embedded_zero, sizeof(embedded_zero), false).valid, "an escaped NUL byte does not index past the escape table");
    check_test_expect(test, !check_literal_character(embedded_zero + 2, 0, false).valid, "an empty character spelling is invalid");
    const uint8_t basic_ucn[] = "'\\u0041'";
    const CheckLiteral old_ucn = check_literal_character(basic_ucn, sizeof(basic_ucn) - 1, false);
    const CheckLiteral new_ucn = check_literal_character(basic_ucn, sizeof(basic_ucn) - 1, true);
    check_test_expect(test, !old_ucn.valid && new_ucn.valid && new_ucn.integer == 65, "basic-character UCNs are accepted only from C23 onward");
    const uint64_t mark = scratch->length;
    const CheckLiteral floating = check_literal_number((const uint8_t*)"0x1.000002p100f", sizeof("0x1.000002p100f") - 1, scratch);
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
        const CheckLiteral half = check_literal_number((const uint8_t*)halves[i].spelling, (uint32_t)strlen(halves[i].spelling), scratch);
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
        const CheckLiteral half = check_literal_number((const uint8_t*)imaginary_halves[i], (uint32_t)strlen(imaginary_halves[i]), scratch);
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
        const CheckLiteral half = check_literal_number((const uint8_t*)invalid_halves[i], (uint32_t)strlen(invalid_halves[i]), scratch);
        check_test_expect(test, !half.valid && !half.resource, "a suffix that only starts like f16 is invalid, as clang has it");
    }
    const CheckLiteral hexadecimal = check_literal_number((const uint8_t*)"0x1f16", sizeof("0x1f16") - 1, scratch);
    check_test_expect(test, hexadecimal.valid && hexadecimal.type == BUSTER_TYPE_INT && hexadecimal.integer == 0x1f16, "a hexadecimal integer ending in f16 is an integer");
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

static uint32_t check_test_leaf(CheckGraph* graph, uint32_t token, CheckValue value)
{
    const uint32_t node = check_graph_add(graph, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    if (node != BUSTER_CHECK_NONE)
    {
        graph->leaves[node] = value;
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
    for (uint32_t batch = partitions ? 1 : BUSTER_CHECK_BATCH; scalar_valid && batch <= BUSTER_CHECK_BATCH; batch += 1)
    {
        const CheckOptions vector_options = {
            .batch_size = batch
        };
        CheckStream vector;
        const bool vector_valid = check_graph_run(table, graph, output, scratch, &vector_options, &vector);
        check_test_expect(test, vector_valid && check_test_stream_equal(&scalar, &vector, graph->argument_count), "graph paths or batch partitions differ");
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
        graph.leaves[assertion] = check_test_integer(BUSTER_TYPE_INT, 0);
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
        graph.count = 2;
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
    CheckTestTypes types[2] = { 0 };
    const bool initialized = check_test_types_open(&types[0]) && check_test_types_open(&types[1]);
    check_test_expect(test, initialized, "independent type tables for scheduling-order test");
    const uint64_t mark = storage->length;
    const uint64_t output_mark = output->length;
    if (initialized)
    {
        TypeId array_int[2];
        TypeId pointer_array_int[2];
        TypeId array_short[2];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            array_int[path] = type_array(&types[path].table, BUSTER_TYPE_INT, 3);
            pointer_array_int[path] = type_pointer(&types[path].table, array_int[path]);
            array_short[path] = type_array(&types[path].table, BUSTER_TYPE_SHORT, 5);
        }
        check_test_expect(test, array_int[0] == array_int[1] && pointer_array_int[0] == pointer_array_int[1] && array_short[0] == array_short[1],
                          "type-order fixture input identities");
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
            const CheckOptions options[2] = {
                {
                    .scalar = true
                },
                {
                    .scalar = false
                },
            };
            CheckStream streams[2];
            const bool first = check_graph_run(&types[0].table, &graph, output, scratch, &options[0], &streams[0]);
            const bool second = check_graph_run(&types[1].table, &graph, output, scratch, &options[1], &streams[1]);
            check_test_expect(test, first && second && check_test_stream_equal(&streams[0], &streams[1], 0), "scheduler changed newly interned TypeIds");
            check_test_expect(test, types[0].table.types.count == types[1].table.types.count, "scheduler constructed a different number of types");
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, mark);
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
        void* columns[BUSTER_ARRAY_LENGTH(widths)];
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
        {
            columns[i] = mappings[i] + readable[i] - widths[i] * count;
        }
        CheckGraph graph = {
            .ops = columns[0],
            .tokens = columns[1],
            .operand0 = columns[2],
            .operand1 = columns[3],
            .operand2 = columns[4],
            .leaves = columns[5],
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
            graph.leaves[node] = check_test_integer(BUSTER_TYPE_UINT, node + 1);
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

static void check_test_source(CheckTest* test, const CheckSourceFixture* fixture, const char* path, const ParseTestClang* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    const PPOptions pp_options = {
        .language = {
            .standard = 2017,
            .gnu = true
        },
        .no_standard_includes = true,
        .semantic_pragmas = true,
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
        ready = ready && parse_initialize(&parsers[p], &parser_options[p], pp_options.language) && check_test_types_open(&types[p]);
        input_storage[p] = arena_reserve(UINT64_C(1) << 24);
        output_storage[p] = arena_reserve(UINT64_C(1) << 24);
        scratch[p] = arena_reserve(UINT64_C(1) << 24);
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
            const bool adapted = check_input(&input[p], &parsers[p], &pp, &types[p].table, &input_storage[p], &scratch[p]);
            char resource_message[256];
            snprintf(resource_message, sizeof(resource_message), "%s: %s source adapter exhausted fixture resources", fixture->name, p == 0 ? "scalar" : "vector");
            check_test_expect(test, !input[p].resource, resource_message);
            rejected[p] = !adapted;
            uint32_t first = BUSTER_CHECK_NONE;
            for (uint32_t i = 0; i < input[p].report_count; i += 1)
            {
                const CheckInputReport* report = &input[p].reports[i];
                const uint32_t source = check_diagnostic_input_report_source(&input[p], &parsers[p], report);
                first = source < first ? source : first;
                unsupported[p] |= report->message == CHECK_INPUT_UNSUPPORTED;
            }
            if (adapted)
            {
                const CheckOptions options = {
                    .scalar = p == 0
                };
                checked[p] = check_graph_run(&types[p].table, &input[p].graph, &output_storage[p], &scratch[p], &options, &streams[p]);
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
                }
            }
            const char* name = NULL;
            uint32_t length = 0;
            lines[p] = first < pp.output.count ? pp_presumed(&pp, pp.output.locations[first], &name, &length) : 0;
        }
        char description[320];
        snprintf(description, sizeof(description), "%s: %s expected, %s from %s path, first line %u (clang %u)", fixture->name,
                 fixture->rejected ? "reject" : "accept", rejected[p] ? "reject" : "accept", p == 0 ? "scalar" : "vector", lines[p], clang->line);
        check_test_expect(test, rejected[p] == fixture->rejected && (!fixture->unsupported || unsupported[p]), description);
        if (!fixture->unsupported && !fixture->extension)
        {
            check_test_expect(test, rejected[p] == clang->rejected && (!rejected[p] || lines[p] == clang->line), description);
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
            // Its alignment applied after its body, which the enum's completion at its base came before: refused.
            "fixed enum: aligned after its body",
            "enum F : char { B } __attribute__((aligned(8)));\n",
            true,
            true
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
    check_test_expect(test, ready, "clang source oracle could not run");
    for (uint32_t i = 0; ready && i < FILE_COUNT; i += 1)
    {
        const CheckSourceFixture* fixture = i < BUSTER_ARRAY_LENGTH(fixtures) ? &fixtures[i] : &random_fixtures[i - BUSTER_ARRAY_LENGTH(fixtures)];
        check_test_expect(test, outcomes[i].rejected == (fixture->rejected && !fixture->unsupported && !fixture->extension), "source fixture expectation differs from clang");
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
        pp.options.predefines.char_unsigned = true;
        const bool target = check_input(&input, &parser, &pp, &table, &storage, &scratch);
        check_test_expect(test, !target && !input.resource && input.graph.count == 0 && input.report_count == 1 && input.reports[0].message == CHECK_INPUT_TARGET,
                          "source adapter rejects a mismatched target model before type construction");
        pp.options.predefines.char_unsigned = false;
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
    check_test_expect(test, !check_input_target_supported(&options), "unsigned plain-char target is explicit unsupported input");
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
#include "c_check_overload_test.c"
#include "c_check_overload_address_test.c"
#include "c_types_function_abi_test.c"
#include "c_check_constant_test.c"
#include "c_check_enum_test.c"
#include "c_check_vla_test.c"
#include "c_check_data_test.c"
#include "c_check_actions_extra_test.c"
#include "c_check_transparent_test.c"
#include "c_check_transparent_type_test.c"
#include "c_check_complex_test.c"
#include "c_check_integer_extension_test.c"
#include "c_check_integer_policy_test.c"
#include "c_check_fp_actions_test.c"
#include "c_check_query_runtime_test.c"
#include "c_check_vector_test.c"
#include "c_check_vector_graph_test.c"
#include "c_check_function_name_test.c"
#include "c_check_pragma_test.c"
#include "c_check_diagnostic_test.c"
#include "c_check_lazy_test.c"
#include "c_check_intrinsic_input_test.c"
#include "c_check_initializer_test.c"
#include "c_check_data_source_test.c"
#include "c_check_attribute_test.c"
#include "c_check_program_test.c"
#include "c_check_prefix_test.c"
#include "c_check_repair_test.c"

static void check_test_source_benchmark(CheckTest* test);
static void check_test_mutations(CheckTest* test, const char* clang);

static int run_checker_tests(int argc, char** argv)
{
    CheckTest test = {
        .random = 133
    };
    const char* clang = "clang";
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
    check_test_expect(&test, options, "usage: test-checker [--seed N] [--clang PATH]");
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_test_expect(&test, initialized, "type-table allocation");
        if (initialized)
        {
            check_test_action_fixtures(&test, &types.table);
            check_test_action_constraints(&test, &types.table);
            check_test_action_regressions(&test, &types.table);
            check_test_update_actions(&test, &types.table);
            check_test_integer_float_rounding(&test, &types.table);
            check_test_action_oracles(&test, clang);
            check_test_actions_batch(&test, &types.table);
            Arena storage = arena_reserve(UINT64_C(1) << 27);
            Arena output = arena_reserve(UINT64_C(1) << 28);
            Arena scratch = arena_reserve(UINT64_C(1) << 27);
            check_test_literals(&test, &scratch);
            check_test_graph_fixtures(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_random(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_limits(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_type_order(&test, &storage, &output, &scratch);
            check_test_graph_arguments(&test, &types.table, &storage, &output, &scratch);
            check_test_graph_guarded(&test, &types.table, &output, &scratch);
            check_test_graph_regressions(&test, &types.table, &storage, &output, &scratch);
            arena_release(&scratch);
            arena_release(&output);
            arena_release(&storage);
            check_test_sources(&test, clang);
            check_test_source_extensions(&test, clang);
            check_test_overloads(&test, clang);
            check_test_overload_sources(&test, clang);
            check_test_overload_addresses(&test, clang);
            check_test_function_abis(&test, clang);
            check_test_constant_sources(&test, clang);
            check_test_enum_sources(&test, clang);
            check_test_vla_sources(&test, clang);
            check_test_type_capacity(&test);
            check_test_target_options(&test);
            check_test_bindings(&test);
            check_test_builtin_graph(&test, &types.table, clang);
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
            check_test_initializers(&test, clang);
            check_test_data_sources(&test);
            check_test_attributes(&test, clang);
            check_test_attribute_sources(&test, clang);
            check_test_programs(&test, clang);
            check_test_prefixes(&test, clang);
            check_test_repairs(&test, clang);
            check_test_mutations(&test, clang);
            check_test_source_benchmark(&test);
        }
        check_test_types_close(&types);
    }
    printf("test-checker: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    return test.failures != 0;
}
