// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Semantic-action fixtures use independently stated C results, and graph tests compare the postorder and level
// schedulers field by field. Floating padding is never compared. The generated actions exercise target arithmetic,
// deferred faults, protected tails and legal batch partitions; source fixtures additionally test the parser adapter.
// Include after c_checker.c and c_check_input.c in the compiler's test unit.
// The checker unit owns the fixtures and their nested includes. Other units read only the value/stream comparisons
// that the corpus shares, and the builtin tests also read the type setup and action helpers. Keep these helpers in
// place so their definitions, attributes and optimization scope stay in the same order as the checker's fixtures.

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
#define BUSTER_CHECK_TEST_BATCH 257u

typedef struct CheckTest CheckTest;
struct CheckTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    // The coded walk's tokens acted at and left to the walk's visit, by class, over the source fixtures' vector path
    // (CheckInput.expression_acted and expression_left, #1038), which check_test_expression_coverage requires of each class.
    uint64_t expression_acted[BUSTER_CHECK_EXPRESSION_CODES];
    uint64_t expression_left[BUSTER_CHECK_EXPRESSION_CODES];
};

// test-checker --part K/N, as test-checker-driver's (CheckDriverPart): the default run's checks are dealt in turn, in
// program order, to N parts, and this process runs part K's (index K - 1), so that the correctness list runs the
// command as N commands. A section deals one unit, and check_test_programs (79 s of the command's 171 s here) one per
// run of up to 12 consecutive programs: dealing each of its 296 on its own added 2.2 s to the checker unit's compile, the
// build's longest, and 27 runs add none. The sections that share the run's type table or graph arenas, whose state
// each leaves to the next, aren't dealt: part 1 runs them all, in their order. Each unit, and each of those sections,
// starts the run's random numbers (CheckTest.random) afresh from the seed and its place among them
// (check_test_part_random), so that a part draws for each the numbers the whole run draws, whatever ran before it.
// Without --part, the one part takes every unit.
typedef struct CheckTestPart CheckTestPart;
struct CheckTestPart
{
    uint32_t index;
    uint32_t count;
    // The units dealt so far, and of them this part's.
    uint32_t dealt;
    uint32_t taken;
    // The sections that share the run's state passed so far, which part 1 runs.
    uint32_t shared;
    // The run's seed, and the test whose numbers each unit starts afresh (none outside run_checker_tests' default run).
    uint64_t seed;
    CheckTest* test;
};

static CheckTestPart check_test_part = { .index = 0, .count = 1 };

// The numbers a unit or a shared section starts from: the seed and its place mixed by splitmix64's finalizer.
static void check_test_part_random(uint64_t place)
{
    if (check_test_part.test != 0)
    {
        uint64_t mixed = check_test_part.seed + (place + 1) * UINT64_C(0x9e3779b97f4a7c15);
        mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94d049bb133111eb);
        check_test_part.test->random = mixed ^ (mixed >> 31);
    }
}

// Whether this part runs the next unit, which it deals.
static bool check_test_takes(void)
{
    const bool taken = check_test_part.dealt % check_test_part.count == check_test_part.index;
    check_test_part_random(check_test_part.dealt);
    check_test_part.dealt += 1;
    check_test_part.taken += taken;
    return taken;
}

// Whether this part runs the next of the sections that share the run's type table or graph arenas: part 1 runs them all.
static bool check_test_shared(void)
{
    check_test_part_random((UINT64_C(1) << 32) | check_test_part.shared);
    check_test_part.shared += 1;
    return check_test_part.index == 0;
}
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
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
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
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
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
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

// A floating constant of `type`: a __float128's of `value`'s bits exactly, with its mirror (c_check_quad.c).
static CheckValue check_test_float(TypeId type, long double value)
{
    CheckValue result = {
        .type = type,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_FLOAT_LITERAL,
        .constant_kind = CHECK_CONSTANT_FLOATING,
        .floating = value,
    };
    if (type_unqualified(type) == BUSTER_TYPE_FLOAT128)
    {
        check_quad_set(&result, check_quad_widen(value, false));
    }
    return result;
}
#endif

static bool check_test_value_equal(const CheckValue* a, const CheckValue* b)
{
    // A _BitInt past 128 bits' constant points to its limbs from the floating payload: the same number from two paths
    // has two pointers (c_check_wide.c).
    const int32_t wide = check_wide_same(a, b);
    const bool floating = wide >= 0 ? wide == 1 : (isnan(a->floating) && isnan(b->floating)) ||
                          (a->floating == b->floating && signbit(a->floating) == signbit(b->floating));
    const bool imaginary = (isnan(a->imaginary) && isnan(b->imaginary)) ||
                           (a->imaginary == b->imaginary && signbit(a->imaginary) == signbit(b->imaginary));
    return a->type == b->type && a->category == b->category && a->flags == b->flags &&
           a->constant_kind == b->constant_kind && a->integer == b->integer && floating && imaginary && a->object == b->object && a->overload_head == b->overload_head &&
           a->offset == b->offset && a->bit_width == b->bit_width && a->fault == b->fault && a->fault_node == b->fault_node &&
           a->object_bit_offset == b->object_bit_offset;
}

// Whether two graphs' leaves of VALUE node `node` are the same value, whichever form each graph holds its leaves in.
#if BUSTER_SOURCE_CONSUMER
static __attribute__((unused)) bool check_test_leaf_equal(const CheckGraph* x, const CheckGraph* y, uint32_t node)
{
    const CheckValue a = check_graph_leaf_value(x, node);
    const CheckValue b = check_graph_leaf_value(y, node);
    return check_test_value_equal(&a, &b);
}
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER) || BUSTER_IN_UNIT(BUSTER_UNIT_CHECK_BUILTINS)
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
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
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
    action.operands[1].type = type_pointer(table, BUSTER_TYPE_UINT | BUSTER_TYPE_CONST);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == CHECK_WARNING_POINTER_SIGN,
                      "pointer signedness warning takes precedence over an ordinary qualifier discard");
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
    action.operands[1] = check_test_integer(BUSTER_TYPE_ULONG, (unsigned __int128)1 << 62);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 11 && result.value.offset == 0,
                      "a 2^62 int index wraps the byte addend to zero");
    CheckValue bytes = address;
    bytes.offset = 0;
    bytes.type = type_pointer(table, BUSTER_TYPE_CHAR);
    action.operands[0] = bytes;
    action.operands[1] = check_test_integer(BUSTER_TYPE_ULONG, (unsigned __int128)1 << 63);
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_ADDRESS && result.value.object == 11 &&
                           result.value.offset == (int64_t)((uint64_t)1 << 63),
                      "a 2^63 byte index is the signed relocation addend");
    action = (CheckAction){
        .op = CHECK_OP_CAST,
        .type = BUSTER_TYPE_BOOL,
        .operands = {
            {
                .type = type_pointer(table, BUSTER_TYPE_CHAR),
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_NONNULL_ADDRESS,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = 11,
                .offset = (int64_t)((uint64_t)1 << 63),
            }
        }
    };
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_INTEGER && result.value.integer == 1,
                      "a non-weak address is true as _Bool, including a wrapped displacement");
    action.operands[0].flags = BUSTER_CHECK_STATIC_OBJECT;
    result = check_test_action(test, table, action);
    check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.value.constant_kind == CHECK_CONSTANT_NONE,
                      "a weak address is not a constant _Bool");
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
    check_test_expect(test, result.diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED, "increment refuses an array by its type, before its modifiability, as clang does");
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
        { "340282366920938463463374607431768211456uwb", 129, true, 0, 0 },
        { "0x8000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000wb", 257, false, 0, 0 },
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

#endif

#if BUSTER_SOURCE_CONSUMER
static bool check_test_stream_equal(const CheckStream* a, const CheckStream* b, uint32_t arguments)
{
    bool equal = a->status == b->status && a->count == b->count && a->report_count == b->report_count && a->effect_count == b->effect_count &&
                 a->varargs_count == b->varargs_count && check_builtin_stream_metadata_equal(a, b, check_test_value_equal);
    for (uint32_t i = 0; equal && i < a->count; i += 1)
    {
        const CheckValue value_a = check_stream_value(a, i);
        const CheckValue value_b = check_stream_value(b, i);
        equal = check_test_value_equal(&value_a, &value_b) && a->diagnostics[i] == b->diagnostics[i] && a->warnings[i] == b->warnings[i] &&
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
#endif

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
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
        check_graph_set_leaf(graph, node, &value);
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
        for (uint32_t path = 0; path < 8; path += 1)
        {
            const CheckOptions options = { .scalar = path % 4 == 0, .verify = path >= 4, .level_order = path % 4 == 2, .regions = path % 4 == 3 };
            CheckStream stream;
            const uint64_t recorded_mark = scratch->length;
            const bool checked = check_graph_run(table, &recorded, output, scratch, &options, &stream);
            check_test_expect(test, checked && check_test_stream_equal(&scalar, &stream, graph->argument_count),
                              "recorded graph facts agree with both validators and all four schedules");
            check_test_expect(test, scratch->length == recorded_mark, "recorded graph run releases only its own scratch");
            arena_pop_to(output, scalar_end);
        }
    }
    arena_pop_to(scratch, scratch_mark);
    // Each batch size by each of the vector path's orders but node order, which takes none: its regions in postorder
    // batches (CheckOptions.regions), its regions by level (CheckOptions.level_order), and its memoized transitions (#1050),
    // every hit verified against its action.
    for (uint32_t run = partitions ? 3 : 3 * BUSTER_CHECK_BATCH; scalar_valid && run < 3 * (BUSTER_CHECK_BATCH + 1); run += 1)
    {
        const uint32_t batch = run / 3;
        const uint32_t order = run % 3;
        const CheckOptions vector_options = {
            .batch_size = batch,
            .regions = order == 0,
            .level_order = order == 1,
            .transitions = order == 2,
            .verify_transitions = true,
        };
        CheckStream vector;
        const bool vector_valid = check_graph_run(table, graph, output, scratch, &vector_options, &vector);
        check_test_expect(test, vector_valid && check_test_stream_equal(&scalar, &vector, graph->argument_count) && vector.statistics.transition_mismatches == 0 &&
                                vector.statistics.transition_verified == vector.statistics.transition_hits,
                          order == 1 ? "graph paths or level-order batch partitions differ" :
                          order == 2 ? "graph paths or the transitions differ" : "graph paths or batch partitions differ");
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
        const CheckValue asserted = check_test_integer(BUSTER_TYPE_INT, 0);
        check_graph_set_leaf(&graph, assertion, &asserted);
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
            check_test_expect(test, check_stream_value(&stream, logical).fault == CHECK_FAULT_NONE && check_stream_value(&stream, logical).integer == 0, "graph short circuit evaluated an unselected operand");
            check_test_expect(test, check_stream_value(&stream, size).integer == 4 && check_stream_value(&stream, size).fault == CHECK_FAULT_NONE, "graph sizeof evaluated its operand");
            check_test_expect(test, check_stream_value(&stream, selected).fault == CHECK_FAULT_DIVIDE_BY_ZERO, "graph lost selected evaluation fault");
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
        graph.leaf_payload_count = 0;
        // All expressions would have the same bracket depth in `a+b+c`. Edges, not parser depths, carry readiness.
        uint32_t previous = check_test_leaf(&graph, 0, check_test_integer(BUSTER_TYPE_UINT, 0));
        for (uint32_t i = 1; i <= 127; i += 1)
        {
            const uint32_t value = check_test_leaf(&graph, i * 2 - 1, check_test_integer(BUSTER_TYPE_UINT, 1));
            previous = check_graph_add(&graph, CHECK_OP_ADD, i * 2, previous, value, BUSTER_CHECK_NONE);
        }
        check_test_compare_graph(test, table, &graph, output, scratch, true);
        const bool chain_valid = check_graph_run(table, &graph, output, scratch, &options, &stream);
        check_test_expect(test, chain_valid && check_stream_value(&stream, previous).integer == 127, "same-bracket-depth dependency chain was evaluated too early");
        arena_pop_to(output, output_mark);
        graph.count = 0;
        graph.leaf_count = 0;
        graph.leaf_payload_count = 0;
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
        graph.leaf_payload_count = 0;
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
    CheckTestTypes types[4] = { 0 };
    const bool initialized = check_test_types_open(&types[0]) && check_test_types_open(&types[1]) && check_test_types_open(&types[2]) &&
                             check_test_types_open(&types[3]);
    check_test_expect(test, initialized, "independent type tables for scheduling-order test");
    const uint64_t mark = storage->length;
    const uint64_t output_mark = output->length;
    if (initialized)
    {
        TypeId array_int[4];
        TypeId pointer_array_int[4];
        TypeId array_short[4];
        for (uint32_t path = 0; path < 4; path += 1)
        {
            array_int[path] = type_array(&types[path].table, BUSTER_TYPE_INT, 3);
            pointer_array_int[path] = type_pointer(&types[path].table, array_int[path]);
            array_short[path] = type_array(&types[path].table, BUSTER_TYPE_SHORT, 5);
        }
        check_test_expect(test, array_int[0] == array_int[1] && pointer_array_int[0] == pointer_array_int[1] && array_short[0] == array_short[1],
                          "type-order fixture input identities");
        check_test_expect(test, array_int[0] == array_int[2] && pointer_array_int[0] == pointer_array_int[2] && array_short[0] == array_short[2],
                          "level-order fixture input identities");
        check_test_expect(test, array_int[0] == array_int[3] && pointer_array_int[0] == pointer_array_int[3] && array_short[0] == array_short[3],
                          "region fixture input identities");
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
            const CheckOptions options[4] = {
                {
                    .scalar = true
                },
                {
                    .scalar = false
                },
                {
                    .level_order = true
                },
                {
                    .regions = true
                },
            };
            CheckStream streams[4];
            const bool first = check_graph_run(&types[0].table, &graph, output, scratch, &options[0], &streams[0]);
            for (uint32_t path = 1; path < 4; path += 1)
            {
                const bool checked = check_graph_run(&types[path].table, &graph, output, scratch, &options[path], &streams[path]);
                check_test_expect(test, first && checked && check_test_stream_equal(&streams[0], &streams[path], 0), "scheduler changed newly interned TypeIds");
                check_test_expect(test, types[0].table.types.count == types[path].table.types.count, "scheduler constructed a different number of types");
            }
        }
    }
    arena_pop_to(output, output_mark);
    arena_pop_to(storage, mark);
    check_test_types_close(&types[3]);
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
            .dense_leaves = columns[5],
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
                graph.dense_leaves[node / 2] = check_test_integer(BUSTER_TYPE_UINT, node + 1);
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
    // Whether the reader must make the unit overload tables (CHECK_TEST_OVERLOAD_TABLES) or none (CHECK_TEST_OVERLOAD_NONE),
    // on both paths: a name of the overloadable attribute's counts only inside an attribute list (#1038). 0 checks neither.
    uint8_t overload_tables;
};

enum
{
    CHECK_TEST_OVERLOAD_TABLES = 1,
    CHECK_TEST_OVERLOAD_NONE = 2,
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

// After every section of a full run: the coded walk (#1038) must have acted by each class's own action somewhere in the
// source fixtures, and left a token of each class whose action declines valid code to the walk's visit; never acted
// by CHECK_INPUT_CODE_SCALAR's, nor left a list's, nor met a DROP's (whose tokens its compaction leaves out). Prints
// each class's counts, among them the tokens no class's action takes.
static __attribute__((noinline)) void check_test_expression_coverage(CheckTest* test)
{
    const char* const* names = check_input_expression_code_names;
    printf("test-checker: the coded walk's classes over the fixtures, acted/left:");
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        const bool named = names[code] != NULL;
        if (named)
        {
            printf(" %s %" PRIu64 "/%" PRIu64, names[code], test->expression_acted[code], test->expression_left[code]);
        }
        // Only the classes the enum names occur.
        const bool counted = test->expression_acted[code] != 0 || test->expression_left[code] != 0;
        // The classes whose actions leave valid code to the walk by design: a K&R unit's or a constexpr object's
        // identifiers, a function's name, a label's address, sizeof or alignof, and K&R calls. An initializer's
        // assignment is its own class, a builtin's token and an intrinsic's call the walk's (check_input_expression_fold),
        // and a statement expression's terminators are the terminators' action's. The others' actions leave a token only where
        // a child has no value or the walk refuses the token (their guard keeps the walk's refusal), and a list's takes
        // every token; DROP's tokens never reach an action.
        const bool declines = code == CHECK_INPUT_CODE_IDENTIFIER || code == CHECK_INPUT_CODE_LITERAL || code == CHECK_INPUT_CODE_PREFIX ||
                              code == CHECK_INPUT_CODE_CALL;
        const bool expected = !named                            ? !counted
                              : code == CHECK_INPUT_CODE_SCALAR ? test->expression_acted[code] == 0
                              : code == CHECK_INPUT_CODE_DROP   ? !counted
                              : code == CHECK_INPUT_CODE_LIST   ? test->expression_acted[code] != 0 && test->expression_left[code] == 0
                                                                : test->expression_acted[code] != 0 && (!declines || test->expression_left[code] != 0);
        char message[192];
        snprintf(message, sizeof(message), "the coded walk's class %u (%s) acted at %" PRIu64 " tokens and left %" PRIu64 " to the walk's visit", code,
                 named ? names[code] : "unnamed", test->expression_acted[code], test->expression_left[code]);
        check_test_expect(test, expected, message);
    }
    printf("\n");
    check_test_expect(test, test->expression_left[CHECK_INPUT_CODE_SCALAR] != 0, "the coded walk leaves the scalar class's tokens to the walk's visit");
}

// Whether the two paths' readers built the same expression graph of a unit of `tokens` tokens: its nodes, their leaves,
// the arguments' slices and the overload calls, and the tokens' nodes and overload heads (#1038: the coded walk's twin,
// the walk, must give the graph node for node, which a typed stream compares only as the checker's results).
static bool check_test_source_graphs_equal(const CheckInput* a, const CheckInput* b, uint32_t tokens)
{
    const CheckGraph* x = &a->graph;
    const CheckGraph* y = &b->graph;
    bool equal = x->count == y->count && x->leaf_count == y->leaf_count && x->argument_count == y->argument_count && x->overload_count == y->overload_count &&
                 (a->overload_heads == NULL) == (b->overload_heads == NULL);
    for (uint32_t node = 0; equal && node < x->count; node += 1)
    {
        equal = x->ops[node] == y->ops[node] && x->tokens[node] == y->tokens[node] && x->operand0[node] == y->operand0[node] &&
                x->operand1[node] == y->operand1[node] && x->operand2[node] == y->operand2[node] && x->types[node] == y->types[node] &&
                x->members[node] == y->members[node] && x->contexts[node] == y->contexts[node] && x->leaf_slots[node] == y->leaf_slots[node] &&
                x->argument_offsets[node] == y->argument_offsets[node] && x->argument_counts[node] == y->argument_counts[node] &&
                (x->source_positions == NULL || x->source_positions[node] == y->source_positions[node]);
        equal = equal && (x->ops[node] != CHECK_OP_VALUE || check_test_leaf_equal(x, y, node));
    }
    for (uint32_t argument = 0; equal && argument < x->argument_count; argument += 1)
    {
        equal = x->argument_nodes[argument] == y->argument_nodes[argument] &&
                (x->argument_tokens == NULL || x->argument_tokens[argument] == y->argument_tokens[argument]);
    }
    for (uint32_t call = 0; equal && call < x->overload_count; call += 1)
    {
        equal = x->overload_calls[call].node == y->overload_calls[call].node && x->overload_calls[call].head == y->overload_calls[call].head;
    }
    for (uint32_t token = 0; equal && token < tokens; token += 1)
    {
        equal = a->token_nodes[token] == b->token_nodes[token] && (a->overload_heads == NULL || a->overload_heads[token] == b->overload_heads[token]);
    }
    return equal;
}

// Whether the two paths' readers listed the same enumeration constants, with their values, and the same enumerations'
// definitions, in token order (check_input_enum_records: the vector path visits the enumerators and the `enum` keywords
// alone, the scalar path every token).
static bool check_test_source_enums_equal(const CheckInput* a, const CheckInput* b)
{
    bool equal = a->enum_value_count == b->enum_value_count && a->enum_definition_count == b->enum_definition_count;
    for (uint32_t i = 0; equal && i < a->enum_value_count; i += 1)
    {
        equal = a->enum_values[i].token == b->enum_values[i].token && a->enum_values[i].value == b->enum_values[i].value;
    }
    for (uint32_t i = 0; equal && i < a->enum_definition_count; i += 1)
    {
        equal = a->enum_definitions[i].type == b->enum_definitions[i].type && a->enum_definitions[i].open == b->enum_definitions[i].open &&
                a->enum_definitions[i].close == b->enum_definitions[i].close;
    }
    return equal;
}

static void check_test_source(CheckTest* test, const CheckSourceFixture* fixture, const char* path, const ParseTestClang* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parsers[2];
    // The oracle's -w (parse_test_clang's): clang then skips its analysis of each function's CFG, and so its misplaced
    // fallthrough's error (check_input_fallthrough_analyzed), which test-checker-driver compares without -w.
    const PPOptions pp_options = {
        .language = {
            .standard = fixture->standard != 0 ? fixture->standard : 2017,
            .gnu = true
        },
        .no_standard_includes = true,
        .semantic_pragmas = true,
        .suppress_warnings = true,
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
            const bool adapted = check_input(&input[p], &parsers[p], &pp, &types[p].table, &input_storage[p], &scratch[p]);
            if (adapted || input[p].diagnostic_graph)
            {
                check_test_input_recorded(test, &input[p].graph, &scratch[p]);
            }
            for (uint32_t code = 0; p == 1 && code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
            {
                test->expression_acted[code] += input[p].expression_acted[code];
                test->expression_left[code] += input[p].expression_left[code];
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
                // The coded walk acts by class (#1038) at tokens of every unit whose features it takes, and the scalar walk at none.
                const uint32_t classed = check_input_expression_classed(&input[1]);
                const uint32_t scalar_counted = check_input_expression_classed(&input[0]) + check_input_expression_declined(&input[0]) +
                                                input[0].expression_left[CHECK_INPUT_CODE_SCALAR];
                snprintf(resource_message, sizeof(resource_message), "%s: the coded walk acts by class at %u of the %u tokens it visits, the scalar walk at %u",
                         fixture->name, classed, input[1].expression_visits, scalar_counted);
                const bool coded = input[1].diagnostic_limits != NULL || input[1].deferred_tokens != NULL ||
                                   (classed != 0 && classed <= input[1].expression_visits);
                check_test_expect(test, coded && scalar_counted == 0, resource_message);
            }
            if (fixture->overload_tables != 0)
            {
                snprintf(resource_message, sizeof(resource_message), "%s: the %s path's reader makes %s", fixture->name, p == 0 ? "scalar" : "vector",
                         fixture->overload_tables == CHECK_TEST_OVERLOAD_TABLES ? "overload tables" : "no overload tables");
                check_test_expect(test, (input[p].overload_heads != NULL) == (fixture->overload_tables == CHECK_TEST_OVERLOAD_TABLES), resource_message);
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
                    .verify = true,
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
            // Compare the primary diagnostic, not a macro-definition spelling. The driver's output diagnostics
            // materialize history and follow this same invocation/argument/generated-token provenance.
            const bool history = first >= pp.output.count || pp_history_ensure(&pp);
            check_test_expect(test, history, "source diagnostic expansion history is available");
            lines[p] = first < pp.output.count && history ? pp_presumed(&pp, parse_primary_location(&pp, first), &name, &length) : 0;
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
        if (!rejected[0] && !rejected[1])
        {
            char graphs[256];
            snprintf(graphs, sizeof(graphs), "%s: the source paths build the same expression graph", fixture->name);
            check_test_expect(test, check_test_source_graphs_equal(&input[0], &input[1], input[0].token_count), graphs);
            snprintf(graphs, sizeof(graphs), "%s: the source paths list the same enumeration constants and definitions", fixture->name);
            check_test_expect(test, check_test_source_enums_equal(&input[0], &input[1]), graphs);
        }
        if (checked[0] && checked[1])
        {
            check_test_expect(test, input[0].graph.argument_count == input[1].graph.argument_count &&
                                   check_test_stream_equal(&streams[0], &streams[1], input[0].graph.argument_count), "source paths produce different typed streams");
            // The vector path by its memoized transitions too (#1050, CheckOptions.transitions), every hit verified.
            const uint64_t mark = output_storage[1].length;
            const CheckOptions memoized = { .verify = true, .transitions = true, .verify_transitions = true };
            CheckStream stream;
            const bool rechecked = check_graph_run(&types[1].table, &input[1].graph, &output_storage[1], &scratch[1], &memoized, &stream);
            check_test_expect(test, rechecked && stream.statistics.transition_mismatches == 0 &&
                                   stream.statistics.transition_verified == stream.statistics.transition_hits &&
                                   check_test_stream_equal(&streams[0], &stream, input[0].graph.argument_count), "source paths' transitions produce another typed stream");
            arena_pop_to(&output_storage[1], mark);
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

// #2053: the shared source fixtures are runtime data, so that editing a case's source or metadata, or adding a case,
// rebuilds nothing. tests/checker/sources (under the fixture root, below) holds catalog.tsv and a file a case: the
// catalog's first line is check_test_fixture_header, and each later one is a case (a line that starts with # is a
// comment), four tab-separated fields: its ID, unique, of letters, digits, '.', '_' and '-'; its file, in the same
// directory, unique, of the same characters, ending in .c; its flags, comma-separated, or - for none; and its name.
// The flags are CheckSourceFixture's fields: rejected, unsupported, extension, gnu23 (standard 2023), unsigned-char,
// undecided, overload-tables, overload-none, and capacity=NAME, a name check_test_fixture_capacity knows. A case's file
// is its source's bytes, read as they are; one with a NUL in it is refused, since the runner writes a source to its first
// NUL. The cases run in the catalog's order. A missing, unreadable or malformed catalog, a missing or unreadable file, a
// duplicate ID or file, a file in the directory that no case names, or no case at all fails the run: no case is
// skipped.
#include <dirent.h>

// The fixture root: tests/ under the working directory, or test-checker's --fixtures DIR.
static const char* check_test_fixture_root = "tests";
static const char check_test_fixture_header[] = "buster-fixtures v1 checker-sources";

enum
{
    // A catalog's cases, and a catalog's or a case file's bytes, at most.
    CHECK_TEST_FIXTURE_CASES = 8192,
    CHECK_TEST_FIXTURE_BYTES = 1 << 20,
};

// A suite's runtime fixtures: its cases, and their names and sources, in `storage`.
typedef struct CheckTestFixtures CheckTestFixtures;
struct CheckTestFixtures
{
    Arena storage;
    CheckSourceFixture* cases;
    const char** files;
    uint32_t count;
    char problem[512];
};

// The type table capacity a catalog's capacity=NAME names, or null. No case names one yet: a case that needs a table
// larger than check_test_types_open's adds its capacity here, by name.
static const TypeCapacity* check_test_fixture_capacity(const char* name)
{
    (void)name;
    return 0;
}

// The bytes of the file at `path`, NUL-terminated, in `storage`, and their count in `length`; null, with the reason in
// `fixtures->problem`, when it can't be read, is larger than CHECK_TEST_FIXTURE_BYTES or holds a NUL.
static char* check_test_fixture_read(CheckTestFixtures* fixtures, const char* path, uint64_t* length)
{
    char* bytes = 0;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool regular = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    if (!regular)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s", path);
    }
    else if (status.st_size > CHECK_TEST_FIXTURE_BYTES)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s is larger than %u bytes", path, (unsigned)CHECK_TEST_FIXTURE_BYTES);
    }
    else
    {
        const uint64_t size = (uint64_t)status.st_size;
        char* buffer = arena_push(&fixtures->storage, size + 1, 1);
        uint64_t done = 0;
        ssize_t got = 1;
        while (buffer != 0 && done < size && got > 0)
        {
            got = read(file, buffer + done, size - done);
            done += got > 0 ? (uint64_t)got : 0;
        }
        if (buffer == 0 || done != size)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s whole", path);
        }
        else if (memchr(buffer, 0, size) != 0)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s holds a NUL byte", path);
        }
        else
        {
            buffer[size] = 0;
            *length = size;
            bytes = buffer;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return bytes;
}

// Whether `text` is 1 to `limit` letters, digits, '.', '_' and '-'.
static bool check_test_fixture_word(const char* text, size_t limit)
{
    const size_t length = strlen(text);
    bool word = length != 0 && length <= limit;
    for (size_t i = 0; word && i < length; i += 1)
    {
        const char c = text[i];
        word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
    }
    return word;
}

// A case's comma-separated flags, or -, into `fixture`; false, with the reason in `fixtures->problem`, for an unknown one.
static bool check_test_fixture_flags(CheckTestFixtures* fixtures, char* flags, CheckSourceFixture* fixture, uint32_t line)
{
    bool known = true;
    if (strcmp(flags, "-") != 0)
    {
        char* flag = flags;
        while (known && flag != 0)
        {
            char* comma = strchr(flag, ',');
            if (comma != 0)
            {
                *comma = 0;
            }
            if (strcmp(flag, "rejected") == 0)
            {
                fixture->rejected = true;
            }
            else if (strcmp(flag, "unsupported") == 0)
            {
                fixture->unsupported = true;
            }
            else if (strcmp(flag, "extension") == 0)
            {
                fixture->extension = true;
            }
            else if (strcmp(flag, "gnu23") == 0)
            {
                fixture->standard = 2023;
            }
            else if (strcmp(flag, "unsigned-char") == 0)
            {
                fixture->char_unsigned = true;
            }
            else if (strcmp(flag, "undecided") == 0)
            {
                fixture->undecided = true;
            }
            else if (strcmp(flag, "overload-tables") == 0 && fixture->overload_tables == 0)
            {
                fixture->overload_tables = CHECK_TEST_OVERLOAD_TABLES;
            }
            else if (strcmp(flag, "overload-none") == 0 && fixture->overload_tables == 0)
            {
                fixture->overload_tables = CHECK_TEST_OVERLOAD_NONE;
            }
            else if (strncmp(flag, "capacity=", 9) == 0 && fixture->capacity == 0 && check_test_fixture_capacity(flag + 9) != 0)
            {
                fixture->capacity = check_test_fixture_capacity(flag + 9);
            }
            else
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "catalog line %u: unknown or repeated flag '%s'", line, flag);
                known = false;
            }
            flag = comma != 0 ? comma + 1 : 0;
        }
    }
    return known;
}

// Whether no file in `directory` but catalog.tsv is outside the catalog's, and each is a regular file.
static bool check_test_fixture_orphans(CheckTestFixtures* fixtures, const char* directory)
{
    DIR* listing = opendir(directory);
    bool clean = listing != 0;
    if (!clean)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't list %s", directory);
    }
    for (struct dirent* entry = clean ? readdir(listing) : 0; clean && entry != 0; entry = readdir(listing))
    {
        const char* name = entry->d_name;
        bool named = strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "catalog.tsv") == 0;
        for (uint32_t i = 0; !named && i < fixtures->count; i += 1)
        {
            named = strcmp(fixtures->files[i], name) == 0;
        }
        if (!named)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s/%s is in no catalog line", directory, name);
            clean = false;
        }
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return clean;
}

// The cases of the catalog `suite` (a directory under the fixture root), and their sources; false, with the reason in
// `fixtures->problem`, when any of it is wrong (the file's first comment).
static bool check_test_fixtures_load(CheckTestFixtures* fixtures, const char* suite)
{
    *fixtures = (CheckTestFixtures){ .storage = arena_reserve(UINT64_C(1) << 28) };
    char directory[PATH_MAX];
    char path[PATH_MAX + 64];
    snprintf(directory, sizeof(directory), "%s/%s", check_test_fixture_root, suite);
    snprintf(path, sizeof(path), "%s/catalog.tsv", directory);
    fixtures->cases = arena_push(&fixtures->storage, sizeof(CheckSourceFixture) * CHECK_TEST_FIXTURE_CASES, 8);
    fixtures->files = arena_push(&fixtures->storage, sizeof(const char*) * CHECK_TEST_FIXTURE_CASES, 8);
    const char** identifiers = arena_push(&fixtures->storage, sizeof(const char*) * CHECK_TEST_FIXTURE_CASES, 8);
    uint64_t length = 0;
    char* catalog = fixtures->cases != 0 && fixtures->files != 0 && identifiers != 0 ? check_test_fixture_read(fixtures, path, &length) : 0;
    bool valid = catalog != 0;
    if (valid && (length == 0 || catalog[length - 1] != '\n'))
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s doesn't end with a newline", path);
        valid = false;
    }
    uint32_t line = 0;
    char* cursor = catalog;
    while (valid && cursor < catalog + length)
    {
        char* end = strchr(cursor, '\n');
        *end = 0;
        line += 1;
        if (line == 1)
        {
            valid = strcmp(cursor, check_test_fixture_header) == 0;
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s's first line isn't '%s'", path, check_test_fixture_header);
            }
        }
        else if (cursor[0] != '#')
        {
            char* fields[4] = { cursor, 0, 0, 0 };
            uint32_t count = 1;
            for (char* tab = strchr(cursor, '\t'); tab != 0 && count < 5; tab = strchr(tab + 1, '\t'))
            {
                *tab = 0;
                if (count < 4)
                {
                    fields[count] = tab + 1;
                }
                count += 1;
            }
            const size_t file_length = count == 4 ? strlen(fields[1]) : 0;
            valid = count == 4 && check_test_fixture_word(fields[0], 64) && check_test_fixture_word(fields[1], 128) && file_length > 2 &&
                    strcmp(fields[1] + file_length - 2, ".c") == 0 && fields[3][0] != 0 && fixtures->count < CHECK_TEST_FIXTURE_CASES;
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u isn't ID, FILE.c, FLAGS and NAME, tab-separated", path, line);
            }
            for (uint32_t i = 0; valid && i < fixtures->count; i += 1)
            {
                valid = strcmp(identifiers[i], fields[0]) != 0 && strcmp(fixtures->files[i], fields[1]) != 0;
                if (!valid)
                {
                    snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u repeats an ID or a file", path, line);
                }
            }
            CheckSourceFixture* fixture = valid ? &fixtures->cases[fixtures->count] : 0;
            if (valid)
            {
                *fixture = (CheckSourceFixture){ .name = fields[3] };
                valid = check_test_fixture_flags(fixtures, fields[2], fixture, line);
            }
            if (valid)
            {
                char source_path[PATH_MAX + 256];
                snprintf(source_path, sizeof(source_path), "%s/%s", directory, fields[1]);
                uint64_t source_length = 0;
                fixture->source = check_test_fixture_read(fixtures, source_path, &source_length);
                valid = fixture->source != 0;
            }
            if (valid)
            {
                identifiers[fixtures->count] = fields[0];
                fixtures->files[fixtures->count] = fields[1];
                fixtures->count += 1;
            }
        }
        cursor = end + 1;
    }
    if (valid && fixtures->count == 0)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s names no case", path);
        valid = false;
    }
    return valid && check_test_fixture_orphans(fixtures, directory);
}

// A suite of runtime source fixtures (#2053) under the fixture root, checked as the shared ones are but without their
// generated cases and variants: each case's source in a temporary file, clang -fsyntax-only -std=gnu17 on them all at
// once, then each case's acceptance against clang's (an unsupported case refuses by name what clang takes, an undecided
// one what clang rejects) and check_test_source's checks on both paths. `oracle` names clang's run in its check. The
// suites' runners were test files' own tables and loops: c_check_cleanup_test.c's, c_check_musttail_test.c's and the
// others that call this.
static __attribute__((noinline)) void check_test_suite(CheckTest* test, const char* clang, const char* suite, const char* oracle)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, suite);
    char problem[sizeof(catalog.problem) + 128];
    snprintf(problem, sizeof(problem), "%s fixtures (#2053): %s", suite, catalog.problem);
    check_test_expect(test, loaded, problem);
    const uint32_t count = loaded ? catalog.count : 0;
    char directory[4096];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-checker-suite-XXXXXX", oracle_temporary_directory());
    char (*paths)[4200] = count != 0 ? arena_push(&catalog.storage, sizeof(*paths) * count, 8) : 0;
    char** pointers = count != 0 ? arena_push(&catalog.storage, sizeof(char*) * count, 8) : 0;
    ParseTestClang* outcomes = count != 0 ? arena_push(&catalog.storage, sizeof(ParseTestClang) * count, 8) : 0;
    const bool made = paths != 0 && pointers != 0 && outcomes != 0 && length > 0 && length < (int)sizeof(directory) && mkdtemp(directory) != 0;
    bool ready = made;
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = parse_test_write(paths[i], catalog.cases[i].source, strlen(catalog.cases[i].source));
    }
    const char* arguments[] = { "-std=gnu17" };
    const bool ran = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, count, outcomes);
    check_test_expect(test, ran, oracle);
    for (uint32_t i = 0; ran && i < count; i += 1)
    {
        const CheckSourceFixture* fixture = catalog.cases + i;
        char description[256];
        snprintf(description, sizeof(description), "%s: expectation differs from Clang", fixture->name);
        check_test_expect(test, outcomes[i].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided)), description);
        check_test_source(test, fixture, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; made && i < count; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (made)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
}

static __attribute__((noinline)) void check_test_sources(CheckTest* test, const char* clang)
{
    CheckTestFixtures catalog;
    const bool loaded = check_test_fixtures_load(&catalog, "checker/sources");
    char problem[sizeof(catalog.problem) + 64];
    snprintf(problem, sizeof(problem), "source fixtures (#2053): %s", catalog.problem);
    check_test_expect(test, loaded, problem);
    const CheckSourceFixture* fixtures = catalog.cases;
    const uint32_t fixture_count = loaded ? catalog.count : 0;
    enum
    {
        RANDOM_COUNT = 64,
    };
    const uint32_t file_count = fixture_count + RANDOM_COUNT;
    char directory[] = "/tmp/buster-checker-XXXXXX";
    char (*paths)[160] = arena_push(&catalog.storage, sizeof(*paths) * file_count, 8);
    char** pointers = arena_push(&catalog.storage, sizeof(char*) * file_count, 8);
    ParseTestClang* outcomes = arena_push(&catalog.storage, sizeof(ParseTestClang) * file_count, 8);
    char** variant_pointers = arena_push(&catalog.storage, sizeof(char*) * file_count, 8);
    uint32_t* variant_fixtures = arena_push(&catalog.storage, sizeof(uint32_t) * file_count, 8);
    ParseTestClang* variant_outcomes = arena_push(&catalog.storage, sizeof(ParseTestClang) * file_count, 8);
    const bool made = loaded && paths != 0 && pointers != 0 && outcomes != 0 && variant_pointers != 0 && variant_fixtures != 0 &&
                      variant_outcomes != 0 && mkdtemp(directory) != 0;
    bool ready = made;
    char random_sources[RANDOM_COUNT][512];
    CheckSourceFixture random_fixtures[RANDOM_COUNT];
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        const char* source;
        if (i < fixture_count)
        {
            source = fixtures[i].source;
        }
        else
        {
            const uint32_t r = i - fixture_count;
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
    ready = ready && parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), pointers, file_count, outcomes);
    // The fixtures in gnu23 or with plain char unsigned, again in their dialect and with their char: gnu23, gnu17 with
    // -funsigned-char, and gnu23 with it.
    static const char* const variant_arguments[3][2] = {
        { "-std=gnu23", "-fsigned-char" },
        { "-std=gnu17", "-funsigned-char" },
        { "-std=gnu23", "-funsigned-char" },
    };
    for (uint32_t v = 0; ready && v < 3; v += 1)
    {
        uint32_t variant_count = 0;
        for (uint32_t i = 0; i < fixture_count; i += 1)
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
    check_test_expect(test, ready || !loaded, "clang source oracle could not run");
    for (uint32_t i = 0; ready && i < file_count; i += 1)
    {
        const CheckSourceFixture* fixture = i < fixture_count ? &fixtures[i] : &random_fixtures[i - fixture_count];
        check_test_expect(test, outcomes[i].rejected == (fixture->rejected && (!fixture->unsupported || fixture->undecided) && !fixture->extension), "source fixture expectation differs from clang");
        check_test_source(test, fixture, paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; made && i < file_count; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        unlink(paths[i]);
    }
    if (made)
    {
        rmdir(directory);
    }
    arena_release(&catalog.storage);
}

// #1130: a scaling fixture's check time for its growth guard: the least thread CPU time of `runs` checks, with that run's
// wall time. Thread CPU time leaves out the time the process is descheduled, which was the flake, and the least of a few
// runs leaves out a page fault or another one-off in the test process; each run does the same work.
#define BUSTER_CHECK_SCALING_RUNS 3u
#define BUSTER_CHECK_SCALING_RATIO 20.0
#define BUSTER_CHECK_SCALING_SLACK 0.2

static double check_test_scaling_seconds(CheckTest* test, const CheckSourceFixture* fixture, const char* path, const ParseTestClang* outcome,
                                         uint32_t runs, double* wall)
{
    double best = 0;
    double best_wall = 0;
    for (uint32_t run = 0; run < runs; run += 1)
    {
        struct timespec cpu_start;
        struct timespec wall_start;
        struct timespec cpu_stop;
        struct timespec wall_stop;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_start);
        clock_gettime(CLOCK_MONOTONIC, &wall_start);
        check_test_source(test, fixture, path, outcome);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_stop);
        clock_gettime(CLOCK_MONOTONIC, &wall_stop);
        const double cpu = (double)(cpu_stop.tv_sec - cpu_start.tv_sec) + (double)(cpu_stop.tv_nsec - cpu_start.tv_nsec) * 1e-9;
        const double elapsed = (double)(wall_stop.tv_sec - wall_start.tv_sec) + (double)(wall_stop.tv_nsec - wall_start.tv_nsec) * 1e-9;
        if (run == 0 || cpu < best)
        {
            best = cpu;
            best_wall = elapsed;
        }
    }
    *wall = best_wall;
    return best;
}

// #480's fifth review: an inferred array's struct element in d parentheses, or after d ?:'s, has its type read from the
// tree's links in one pass, not from a scan of what's left at each level. Against clang at depths it parses (its
// brackets stop at 256), then alone: 60,000 parentheses and 40,000 ?:'s must check within 20 times the thread CPU time a
// tenth takes, plus 0.2 s, as linear work does (#1130). Thread CPU time excludes descheduling, which was the flake, and
// the least of three checks drops a page fault or another one-off; the times are printed. The scans took 0.62 s for
// 30,000 parentheses on the laptop and 1.16 s for 30,000 ?:'s, 9 times as long as for 10,000, which a tenth's 20 times
// didn't allow.
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
        double wall[2] = { 0 };
        char description[256];
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
            snprintf(description, sizeof(description), "%u %s around an inferred array's struct element: %s", depth, levels,
                     timing ? "the fixture is written" : "clang accepts it");
            check_test_expect(test, ready && !outcome.rejected, description);
            if (ready)
            {
                const CheckSourceFixture fixture = {
                    .name = name,
                    .source = (const char*)text.base,
                };
                if (timing)
                {
                    const uint32_t index = c - BUSTER_ARRAY_LENGTH(compared);
                    seconds[index] = check_test_scaling_seconds(test, &fixture, path, &outcome, BUSTER_CHECK_SCALING_RUNS, &wall[index]);
                }
                else
                {
                    check_test_source(test, &fixture, path, &outcome);
                }
            }
            unlink(path);
            arena_release(&text);
        }
        snprintf(description, sizeof(description),
                 "%u %s around an inferred array's struct element: %.3f s of thread CPU (%.3f s wall) in the best of %u checks, within "
                 "20 times %u's %.3f s (%.3f s wall) plus %.1f s",
                 timed[form], levels, seconds[1], wall[1], BUSTER_CHECK_SCALING_RUNS, timed[form] / 10, seconds[0], wall[0], BUSTER_CHECK_SCALING_SLACK);
        check_test_expect(test, seconds[1] <= BUSTER_CHECK_SCALING_RATIO * seconds[0] + BUSTER_CHECK_SCALING_SLACK, description);
        printf("test-checker: %s\n", description);
    }
    if (created)
    {
        rmdir(directory);
    }
}

// #591: member accesses checked in time linear in the unit, whatever the type table's size and the records'. The lookup
// cleared a byte per type at every access, and compared the record's names one at a time. Two units, each against clang
// at a small size, then measured at a tenth of a size and at the size, both paths (check_test_source, whose typed
// streams, the members' offsets among them, must agree): the larger must check within 20 times the thread CPU time a
// tenth takes, plus 0.2 s, as linear work does (#1130). Thread CPU time excludes descheduling, which was the flake, and
// the least of three checks drops a page fault or another one-off; the times are printed.
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
        double wall[2] = { 0 };
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
            char description[240];
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
                };
                if (timing)
                {
                    const uint32_t index = c - 1;
                    seconds[index] = check_test_scaling_seconds(test, &fixture, path, &outcome, BUSTER_CHECK_SCALING_RUNS, &wall[index]);
                }
                else
                {
                    check_test_source(test, &fixture, path, &outcome);
                }
            }
            unlink(path);
            arena_release(&text);
        }
        char description[320];
        snprintf(description, sizeof(description),
                 "%s: %u read and checked in %.3f s of thread CPU (%.3f s wall) in the best of %u checks, within 20 times %u's %.3f s (%.3f s "
                 "wall) plus %.1f s",
                 name, timed[form], seconds[1], wall[1], BUSTER_CHECK_SCALING_RUNS, timed[form] / 10, seconds[0], wall[0], BUSTER_CHECK_SCALING_SLACK);
        check_test_expect(test, seconds[1] <= BUSTER_CHECK_SCALING_RATIO * seconds[0] + BUSTER_CHECK_SCALING_SLACK, description);
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
                                   !check_stream_has_values(&stream) && output.length == output_mark && scratch.length == scratch_mark,
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

// A column of `count` tokens with `attributes`' entries, for tokens 0 to `count` - 1, in its own pool in `arena`: for the
// tests that build a CheckInput's attributes from a pointer a token. Returns a null column where the arena has no room.
static CheckSparseWide check_input_attributes_from(Arena* arena, CheckInputAttributes* const* attributes, uint32_t count)
{
    CheckSparsePool* pool = NULL;
    CheckSparseWide column = { 0 };
    if (attributes != NULL && check_sparse_reserve_wide(arena, &pool, NULL, 0, &column, 1, count, false, false))
    {
        for (uint32_t token = 0; token < count; token += 1)
        {
            check_sparse_wide_set(pool, column, token, ~(uint64_t)(uintptr_t)attributes[token]);
        }
    }
    return column;
}

#include "c_check_graph_test.c"
#include "c_check_stream_columns_test.c"
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
#include "c_check_wide_test.c"
#include "c_check_sizeof_test.c"
#include "c_check_transitions_test.c"
#include "c_check_integer_policy_test.c"
#include "c_check_fp_actions_test.c"
#include "c_check_query_runtime_test.c"
#include "c_check_vector_test.c"
#include "c_check_vector_graph_test.c"
#include "c_check_function_name_test.c"
#include "c_check_pragma_test.c"
#include "c_check_pack_test.c"
#include "c_check_diagnostic_test.c"
#include "c_check_warning_events_test.c"
#include "c_check_const_values_test.c"
#include "c_check_context_test.c"
#include "c_check_context_source_test.c"
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
#include "c_check_deprecated_walk_test.c"
#include "c_check_program_test.c"
#include "c_check_direct_initializer_test.c"
#include "c_check_typeof_compound_test.c"
#include "c_check_cleanup_test.c"
#include "c_check_musttail_test.c"
#include "c_check_program_bits_test.c"
#include "c_check_program_generic_builtins_test.c"
#include "c_check_program_align_test.c"
#include "c_check_const_source_test.c"
#include "c_check_const_alias_test.c"
#include "c_check_comma_input_test.c"
#include "c_check_embed_test.c"
#include "c_check_prefix_test.c"
#include "c_check_repair_test.c"
#include "c_check_system_tokens_test.c"
#include "c_check_input_scan_test.c"
#include "c_check_warning_input_test.c"
#include "c_check_selection_test.c"
#include "c_check_program_library_test.c"
#include "c_check_program_classify_test.c"
#include "c_check_program_constants_test.c"

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
        const uint32_t vector = check_input_builtin_capacity(NULL, builtins, count, false);
        const uint32_t scalar = check_input_builtin_capacity(NULL, builtins, count, true);
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
static void check_test_lowering_inventory(CheckTest* test);
static void check_test_node_census(CheckTest* test);
static void check_test_attested(CheckTest* test);
static void check_test_lowering_templates(CheckTest* test);
static void check_test_backend_source_benchmark(CheckTest* test);
static void check_test_check_lanes_benchmark(CheckTest* test);
static void check_test_requests(CheckTest* test);
static void check_test_corpus_symbol_reasons(CheckTest* test, const char* clang);
static void check_test_mutations(CheckTest* test, const char* clang);
static void check_test_constraints(CheckTest* test, const char* clang);
static void check_test_semantic_diagnostics(CheckTest* test, const char* clang);
static void check_test_x86_diagnostics(CheckTest* test, TypeTable* types, const char* clang);
static void check_test_undeclared(CheckTest* test, const char* clang);

static int run_checker_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
    CheckTest test = {
        .random = 133
    };
    const char* clang = "clang";
    bool direct_initializers = false;
    bool program_initializers = false;
    bool program_selection = false;
    bool const_values = false;
    bool program_vectors = false;
    bool program_amx = false;
    bool program_asm = false;
    bool program_bits = false;
    bool program_align = false;
    bool program_float_constants = false;
    bool program_overloads = false;
    bool program_library = false;
    bool embed_initializers = false;
    bool requests = false;
    bool attested = false;
    bool sizeof_only = false;
    bool transitions_only = false;
    bool semantic_diagnostics = false;
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
        else if (strcmp(argv[i], "--direct-initializers") == 0)
        {
            direct_initializers = true;
        }
        else if (strcmp(argv[i], "--program-initializers") == 0)
        {
            program_initializers = true;
        }
        else if (strcmp(argv[i], "--program-selection") == 0)
        {
            program_selection = true;
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
        else if (strcmp(argv[i], "--program-float-constants") == 0)
        {
            program_float_constants = true;
        }
        else if (strcmp(argv[i], "--embed-initializers") == 0)
        {
            embed_initializers = true;
        }
        else if (strcmp(argv[i], "--sizeof") == 0)
        {
            sizeof_only = true;
        }
        else if (strcmp(argv[i], "--requests") == 0)
        {
            requests = true;
        }
        else if (strcmp(argv[i], "--attested") == 0)
        {
            attested = true;
        }
        else if (strcmp(argv[i], "--semantic-diagnostics") == 0)
        {
            semantic_diagnostics = true;
        }
        else if (strcmp(argv[i], "--transitions") == 0)
        {
            transitions_only = true;
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[++i];
            options = *clang != 0;
        }
        else if (strcmp(argv[i], "--fixtures") == 0 && i + 1 < argc)
        {
            // The runtime fixtures' root (#2053), tests/ under the working directory unless given.
            check_test_fixture_root = argv[++i];
            options = *check_test_fixture_root != 0;
        }
        else if (strcmp(argv[i], "--part") == 0 && i + 1 < argc)
        {
            unsigned index = 0;
            unsigned count = 0;
            int used = 0;
            const char* text = argv[++i];
            options = sscanf(text, "%u/%u%n", &index, &count, &used) == 2 && text[used] == 0 && index >= 1 && index <= count && count <= 64;
            check_test_part = (CheckTestPart){ .index = options ? index - 1 : 0, .count = options ? count : 1 };
        }
        else
        {
            options = false;
        }
    }
    // Each unit of the default run starts its numbers afresh from the seed (check_test_takes).
    check_test_part.seed = test.random;
    check_test_part.test = &test;
    // The selections run their own checks whole: --part deals the default run's.
    options = options && (check_test_part.count == 1 || !(direct_initializers || program_initializers || const_values || program_vectors || program_amx || program_asm ||
                                                           program_bits || program_align || program_float_constants || program_overloads ||
                                                           program_library || embed_initializers || requests || attested || sizeof_only || transitions_only || semantic_diagnostics ||
                                                           program_selection));
    CheckTestTypes types = { 0 };
    check_test_expect(&test, options, "usage: test-checker [--seed N] [--clang PATH] [--fixtures DIR] [--part K/N] [--direct-initializers|--program-initializers|--program-selection|--program-vectors|--program-amx|--program-asm|--program-bits|--program-align|--program-float-constants|--program-overloads|--program-library|--embed-initializers|--requests|--attested|--const-values|--sizeof|--transitions|--semantic-diagnostics]");
    // The process's oracle cache, set up before any check runs clang through it (oracle_cache.c, #542).
    OracleCache* oracle = oracle_cache();
    if (options)
    {
        const bool initialized = check_test_types_open(&types);
        check_test_expect(&test, initialized, "type-table allocation");
        if (initialized && direct_initializers)
        {
            check_test_direct_initializers(&test, clang);
        }
        else if (initialized && semantic_diagnostics)
        {
            check_test_semantic_diagnostics(&test, clang);
        }
        else if (initialized && transitions_only)
        {
            check_test_transitions(&test);
        }
        else if (initialized && sizeof_only)
        {
            check_test_sizeof_scaling(&test);
            check_test_integer_extensions(&test, clang);
            check_test_vla_sources(&test, clang);
        }
        else if (initialized && const_values)
        {
            check_test_const_values(&test);
            check_test_semantic_context(&test);
            check_test_semantic_context_sources(&test, clang);
            check_test_const_sources(&test, clang);
            check_test_const_alias_sources(&test, clang);
            check_test_const_programs(&test, clang);
        }
        else if (initialized && embed_initializers)
        {
            check_test_embed_initializers(&test, clang);
        }
        else if (initialized && requests)
        {
            // #1057: one request's configuration and the shared source preparation against the driver, then the source
            // benchmarks' preparations, which share them.
            check_test_requests(&test);
            check_test_lowering_source_benchmark(&test);
            check_test_backend_source_benchmark(&test);
            check_test_check_lanes_benchmark(&test);
        }
        else if (initialized && attested)
        {
            // #2531's census (measure-attested), alone.
            check_test_attested(&test);
        }
        else if (initialized && program_selection)
        {
            check_test_program_selection(&test, clang);
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
        else if (initialized && program_float_constants)
        {
            check_test_float_constants(&test, clang);
        }
        else if (initialized)
        {
            if (check_test_shared())
            {
                check_test_action_fixtures(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_action_constraints(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_action_regressions(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_update_actions(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_integer_float_rounding(&test, &types.table);
            }
            if (check_test_takes())
            {
                check_test_action_oracles(&test, clang);
            }
            if (check_test_shared())
            {
                check_test_actions_batch(&test, &types.table);
            }
            if (check_test_takes())
            {
                check_test_warning_events(&test);
            }
            if (check_test_takes())
            {
                check_test_const_values(&test);
            }
            if (check_test_takes())
            {
                check_test_semantic_context(&test);
            }
            if (check_test_takes())
            {
                check_test_semantic_context_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_warning_deferred(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_set(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_policy(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_render(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_reader(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_graph(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_initializers(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_undefined(&test);
            }
            if (check_test_takes())
            {
                check_test_const_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_const_alias_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_const_programs(&test, clang);
            }
            Arena storage = arena_reserve(UINT64_C(1) << 27);
            Arena output = arena_reserve(UINT64_C(1) << 28);
            Arena scratch = arena_reserve(UINT64_C(1) << 27);
            if (check_test_shared())
            {
                check_test_literals(&test, &scratch);
            }
            if (check_test_shared())
            {
                check_test_literal_warnings(&test, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_fixtures(&test, &types.table, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_random(&test, &types.table, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_recorded(&test, &types.table, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_limits(&test, &types.table, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_type_order(&test, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_arguments(&test, &types.table, &storage, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_guarded(&test, &types.table, &output, &scratch);
            }
            if (check_test_shared())
            {
                check_test_graph_regressions(&test, &types.table, &storage, &output, &scratch);
            }
            arena_release(&scratch);
            arena_release(&output);
            arena_release(&storage);
            if (check_test_takes())
            {
                check_test_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_typeof_compounds(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_constraints(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_initializer_nesting(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_member_scaling(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_source_extensions(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_record_names(&test);
            }
            if (check_test_takes())
            {
                check_test_overloads(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_overload_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_overload_addresses(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_function_abis(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_constant_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_generic_index(&test);
            }
            if (check_test_takes())
            {
                check_test_generic_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_enum_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_vla_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_type_capacity(&test);
            }
            if (check_test_takes())
            {
                check_test_stream_columns(&test);
            }
            if (check_test_takes())
            {
                check_test_fresh_output(&test);
            }
            if (check_test_takes())
            {
                check_test_target_options(&test);
            }
            if (check_test_takes())
            {
                check_test_bindings(&test);
            }
            if (check_test_shared())
            {
                check_test_builtin_graph(&test, &types.table, clang);
            }
            if (check_test_takes())
            {
                check_test_builtin_capacity(&test);
            }
            if (check_test_takes())
            {
                check_test_system_tokens(&test);
                check_test_input_scans(&test);
            }
            if (check_test_takes())
            {
                check_test_identifier_leaves(&test);
                check_test_literal_values(&test);
                check_test_expression_flush(&test);
            }
            if (check_test_takes())
            {
                check_test_selection_positions(&test);
            }
            if (check_test_takes())
            {
                check_test_asm_label_kernels(&test);
            }
            if (check_test_takes())
            {
                check_test_expression_commas(&test);
            }
            if (check_test_takes())
            {
                check_test_warning_ranges(&test);
            }
            if (check_test_takes())
            {
                check_test_selection_roots(&test);
            }
            if (check_test_shared())
            {
                check_test_varargs_graph(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_special_graph(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_builtin_vector_graph(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_x86_graph(&test, &types.table, clang);
            }
            if (check_test_takes())
            {
                check_test_environments(&test);
            }
            if (check_test_takes())
            {
                check_test_body_binding_guards(&test);
            }
            if (check_test_takes())
            {
                check_test_body_binding_exit_points(&test);
            }
            if (check_test_shared())
            {
                check_test_data(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_actions_extra(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_transparent_calls(&test, &types.table, clang);
            }
            if (check_test_takes())
            {
                check_test_transparent_types(&test, clang);
            }
            if (check_test_shared())
            {
                check_test_complex(&test, &types.table, clang);
            }
            if (check_test_takes())
            {
                check_test_integer_extensions(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_wide(&test, &types.table);
            }
            if (check_test_takes())
            {
                check_test_sizeof_scaling(&test);
            }
            if (check_test_takes())
            {
                check_test_transitions(&test);
            }
            if (check_test_shared())
            {
                check_test_integer_policy(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_fp_actions(&test, &types.table);
            }
            if (check_test_shared())
            {
                check_test_query_runtime(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_vectors(&test, &types.table, clang);
            }
            if (check_test_shared())
            {
                check_test_vector_graph(&test, &types.table);
            }
            if (check_test_takes())
            {
                check_test_function_names(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_pragmas(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_pack_pragmas(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_diagnostic_policy(&test);
            }
            if (check_test_takes())
            {
                check_test_lazy(&test);
            }
            if (check_test_takes())
            {
                check_test_intrinsic_inputs(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_x86_diagnostics(&test, &types.table, clang);
            }
            if (check_test_takes())
            {
                check_test_undeclared(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_initializers(&test, clang);
                check_test_direct_initializers(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_embed_initializers(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_data_sources(&test);
            }
            if (check_test_takes())
            {
                check_test_attributes(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_attribute_sources(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_deprecated_walks(&test);
            }
            // Deals its programs itself.
            check_test_programs(&test, clang);
            if (check_test_takes())
            {
                check_test_cleanups(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_musttails(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_bits(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_align(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_program_initializers(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_library(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_classify(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_float_constants(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_prefixes(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_repairs(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_mutations(&test, clang);
            }
            if (check_test_takes())
            {
                check_test_source_benchmark(&test);
            }
            if (check_test_takes())
            {
                check_test_lowering_source_benchmark(&test);
            }
            if (check_test_takes())
            {
                check_test_lowering_inventory(&test);
            }
            if (check_test_takes())
            {
                check_test_node_census(&test);
            }
            if (check_test_takes())
            {
                check_test_attested(&test);
            }
            if (check_test_takes())
            {
                check_test_lowering_templates(&test);
            }
            if (check_test_takes())
            {
                check_test_backend_source_benchmark(&test);
            }
            if (check_test_takes())
            {
                check_test_check_lanes_benchmark(&test);
            }
            if (check_test_takes())
            {
                check_test_requests(&test);
            }
            if (check_test_takes())
            {
                check_test_corpus_symbol_reasons(&test, clang);
            }
            // The coded walk's classes (#1038) need every source fixture's counts: a part has only its own.
            if (check_test_part.count == 1)
            {
                check_test_expression_coverage(&test);
            }
        }
        // The program harness's build directory, which the sections' programs share: removed after the last of them,
        // whichever sections ran.
        check_test_program_build_release();
        check_test_types_close(&types);
    }
    check_test_expect(&test, check_test_part.count == 1 || check_test_part.taken != 0, "the checker test's part runs some of its checks");
    oracle_cache_report(oracle, "test-checker");
    if (check_test_part.count > 1)
    {
        printf("test-checker: part %u of %u: %u of the %u units dealt\n", check_test_part.index + 1, check_test_part.count, check_test_part.taken,
               check_test_part.dealt);
    }
    printf("test-checker: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int returned = test.failures != 0;
    check_test_part.test = 0;
    oracle_epoch_end(epoch);
    return returned;
}
#endif
