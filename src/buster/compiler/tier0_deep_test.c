// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Values past Tier 0's depth tables (#136's item 6, tier0_select_deep), on the cases the generated programs reach
// rarely or never: functions of one U64 parameter (x, in rdi) whose stacks fill their tables and then cross the line
// with an entry of each kind, compiled by both paths into the same records, encoded by both encoders and run natively on
// six inputs against this file's model of each. tier0_test.c includes it after tier0_x87_test.c, for its "deep" check.

#define BUSTER_TIER0_TEST_DEEP_ENTRIES 256
#define BUSTER_TIER0_TEST_DEEP_INPUTS 6
#define BUSTER_TIER0_TEST_DEEP_CASES 11
// The pages of each case's code.
#define BUSTER_TIER0_TEST_DEEP_PAGES 16

typedef struct Tier0TestDeep Tier0TestDeep;
struct Tier0TestDeep
{
    uint8_t kinds[BUSTER_TIER0_TEST_DEEP_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_DEEP_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_DEEP_ENTRIES];
    uint32_t count;
};

static uint32_t tier0_test_deep_add(Tier0TestDeep* deep, uint8_t kind, uint8_t class, uint64_t value)
{
    const uint32_t index = deep->count;
    deep->kinds[index] = kind;
    deep->classes[index] = class;
    deep->values[index] = value;
    deep->count += 1;
    return index;
}

// x op k, an integer value: the parameter, the constant and the operation.
static void tier0_test_deep_operation(Tier0TestDeep* deep, uint8_t kind, uint64_t k)
{
    tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, k);
    tier0_test_deep_add(deep, kind, BUSTER_TYPED_CLASS_U64, 0);
}

// (double)((x & 0xffff) + k), a floating value: through a conversion from U64, tier0_emit_u64_convert's.
static void tier0_test_deep_floating(Tier0TestDeep* deep, uint64_t k)
{
    tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 0xffff);
    tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, k);
    tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, 0);
}

// `count` additions of a class: the stack's values folded into one, the top first.
static void tier0_test_deep_fold(Tier0TestDeep* deep, uint8_t class, uint32_t count)
{
    for (uint32_t k = 0; k < count; k += 1)
    {
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, class, 0);
    }
}

// local 1 + k, an F64 value (case 8's): the constant through the integer register at the integer stack's depth.
static void tier0_test_deep_local_plus(Tier0TestDeep* deep, uint32_t k)
{
    const double constant = (double)k;
    uint64_t bits = 0;
    memcpy(&bits, &constant, sizeof(bits));
    tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F64, 1);
    tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, bits);
    tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F64, 0);
}

// Case c's stream, and its name. Each starts with x + k for k from 0 on the integer stack, 15 of them, which fill its
// table (case 5's 14, a frame pointer's table, and case 9's, a realigned frame's; case 7's 15 are past it; case 8 holds
// 13, and so does case 10, a realigned frame's with a base, r15, the table's last but rbp), and returns a U64. Cases 9
// and 10 have an object (local 4, of 16 bytes) aligned to 64 and to 4096, which realigns their frames.
static const char* tier0_test_deep_case(uint32_t c, Tier0TestDeep* deep)
{
    static const char* const names[BUSTER_TIER0_TEST_DEEP_CASES] = {
        "a value dropped at the line",
        "?: at the line",
        "a compare and its branch at the line",
        "floating values at the line",
        "division and shifts at the line",
        "a frame pointer's 14",
        "both stacks at once",
        "rsp moved past the line",
        "a conversion past the floating line",
        "a realigned frame's 14",
        "a realigned frame's base past the line",
    };
    deep->count = 0;
    if (c == 10)
    {
        // The stack's position, which with the object aligned to 4096 gives the function a base, kept in local 1.
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
    }
    if (c == 9 || c == 10)
    {
        // The object's first word: x.
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (c == 5)
    {
        // The stack's position, which gives the function its frame pointer, kept in local 1.
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
    }
    else if (c == 4)
    {
        // A divisor in local 1: (x & 7) + 1.
        tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 7);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
    }
    else if (c == 8)
    {
        // (double)(x & 0xffff) in local 1, an F64 one, then local 1 + k for k from 0 to 15, which fill the floating table,
        // with the integer stack empty.
        tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 0xffff);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_F64, 1);
        for (uint32_t k = 0; k < 16; k += 1)
        {
            tier0_test_deep_local_plus(deep, k);
        }
    }
    const uint32_t held = c == 5 || c == 9 ? 14 : c == 8 || c == 10 ? 13 : 15;
    for (uint32_t k = 0; k < held; k += 1)
    {
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, k);
    }
    uint32_t folds = held - 1;
    if (c == 0)
    {
        // -x pushed past the line, dropped back to it (a DISCARD, which uses no register), then ~x past it.
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_U64, 0);
        folds += 1;
    }
    else if (c == 1 || c == 2)
    {
        // (x & 1 ? x * 7 : x - 5), or ((x + 1 < x * 3) ? x ^ 13 : 11): a condition past the line, its branch back to it,
        // and each arm's value past it.
        uint32_t branch = 0;
        if (c == 1)
        {
            tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 1);
            branch = tier0_test_deep_add(deep, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_deep_operation(deep, BUSTER_TYPED_MUL, 7);
        }
        else
        {
            tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 1);
            tier0_test_deep_operation(deep, BUSTER_TYPED_MUL, 3);
            tier0_test_deep_add(deep, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
            branch = tier0_test_deep_add(deep, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 11);
        }
        const uint32_t jump = tier0_test_deep_add(deep, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_U64, 0);
        deep->values[branch] = deep->count;
        tier0_test_deep_operation(deep, c == 1 ? BUSTER_TYPED_SUB : BUSTER_TYPED_XOR, c == 1 ? 5 : 13);
        deep->values[jump] = deep->count;
        folds += 1;
    }
    else if (c == 3)
    {
        // 16 floating values, which fill the floating table; one more past the line, dropped, and another; their sum.
        for (uint32_t k = 0; k < 16; k += 1)
        {
            tier0_test_deep_floating(deep, k);
        }
        tier0_test_deep_floating(deep, 100);
        tier0_test_deep_add(deep, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0);
        tier0_test_deep_floating(deep, 16);
        tier0_test_deep_fold(deep, BUSTER_TYPED_CLASS_F64, 16);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        folds += 1;
    }
    else if (c == 4)
    {
        // (x + 1000003) / d with d a local, x % d, x << (x & 7), x / (d + 0) and x >> d with d on the stack.
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 1000003);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_MOD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 7);
        tier0_test_deep_add(deep, BUSTER_TYPED_SHL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        folds += 1;
    }
    else if (c == 5)
    {
        // Past the frame pointer's line, two more.
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 14);
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 15);
        folds += 2;
    }
    else if (c == 7)
    {
        // Past a frame pointer's line (#434's review): rsp saved in local 1; (x & 0xff) * 8 + 24 bytes allocated, their
        // address in local 2, and 4096 more in local 3; x + 1000 stored in the first and x ^ 0x5555 in the second, and
        // both loaded back; rsp restored; then x * 77. Every entry is a deep one, with the values below it in their slots,
        // which rbp addresses while rsp moves, and so are the locals' homes.
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_operation(deep, BUSTER_TYPED_AND, 0xff);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8);
        tier0_test_deep_add(deep, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 24);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 1000);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4096);
        tier0_test_deep_add(deep, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_test_deep_operation(deep, BUSTER_TYPED_XOR, 0x5555);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_operation(deep, BUSTER_TYPED_MUL, 77);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        folds += 1;
    }
    else if (c == 9)
    {
        // Past the line: the object's address modulo 64 (0), its first word (x) and x + 15.
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 63);
        tier0_test_deep_add(deep, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_operation(deep, BUSTER_TYPED_ADD, 15);
        folds += 3;
    }
    else if (c == 10)
    {
        // Past the base's line: 4104 bytes allocated (rounded up to the frame's 4096), their address in local 2, x ^ 3 stored
        // there; the object's address modulo 4096 and the allocation's modulo 16 (both 0), the object's first word (x)
        // and the allocation's (x ^ 3); rsp restored, while every place of the frame is r15's.
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4104);
        tier0_test_deep_add(deep, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_operation(deep, BUSTER_TYPED_XOR, 3);
        tier0_test_deep_add(deep, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4095);
        tier0_test_deep_add(deep, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 15);
        tier0_test_deep_add(deep, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
        folds += 4;
    }
    else if (c == 8)
    {
        // Over the 13 integers, a 17th floating value past the floating table, local 1 + 16, its constant through r15, the
        // integer register at depth 13; then converted to U64 past the floating line, at integer depth 13 (#429's review's
        // limits): a deep entry on the floating stack alone, whose form selection chose at its depths before its window's,
        // the red zone's. The register form, which its window's depths would choose, has its scratch at depth 14, rbp,
        // which nothing here reaches: the prologue doesn't save it unless it is x's home, which the last addition reads.
        // Then the integers folded and converted to F64, past the floating line again, and added to the 16 floating
        // values, their sum converted to U64, and x added.
        tier0_test_deep_local_plus(deep, 16);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_fold(deep, BUSTER_TYPED_CLASS_U64, held);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, 0);
        tier0_test_deep_fold(deep, BUSTER_TYPED_CLASS_F64, 16);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
        tier0_test_deep_add(deep, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        folds = 0;
    }
    else
    {
        // 16 floating values; x & 0xffff past the integer line, converted past the floating one (its operand dropped back
        // to the integer line); the floating values' sum converted back past the integer line.
        for (uint32_t k = 0; k < 16; k += 1)
        {
            tier0_test_deep_floating(deep, k);
        }
        tier0_test_deep_floating(deep, 16);
        tier0_test_deep_fold(deep, BUSTER_TYPED_CLASS_F64, 16);
        tier0_test_deep_add(deep, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        folds += 1;
    }
    tier0_test_deep_fold(deep, BUSTER_TYPED_CLASS_U64, folds);
    if (c == 5)
    {
        tier0_test_deep_add(deep, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_deep_add(deep, BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0);
    }
    tier0_test_deep_add(deep, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    return names[c];
}

// The floating values' sum of cases 3 and 6: (double)((x & 0xffff) + k) for k from 0 to 16, added from the top.
static uint64_t tier0_test_deep_floating_sum(uint64_t x)
{
    double sum = (double)((x & 0xffff) + 16);
    for (uint32_t k = 16; k-- > 0;)
    {
        sum = (double)((x & 0xffff) + k) + sum;
    }
    return (uint64_t)sum;
}

// What case c returns for x.
static uint64_t tier0_test_deep_model(uint32_t c, uint64_t x)
{
    const uint32_t held = c == 5 || c == 9 ? 14 : c == 8 || c == 10 ? 13 : 15;
    // Case 8's: the integers and its conversion's result, as a double, added to its 16 floating values from the top.
    uint64_t integers = (x & 0xffff) + 16;
    for (uint32_t k = 0; k < 13; k += 1)
    {
        integers += x + k;
    }
    double floating = (double)integers;
    for (uint32_t k = 16; k-- > 0;)
    {
        floating = ((double)(x & 0xffff) + (double)k) + floating;
    }
    uint64_t sum = 0;
    for (uint32_t k = 0; k < held; k += 1)
    {
        sum += x + k;
    }
    const uint64_t d = (x & 7) + 1;
    const uint64_t cases[BUSTER_TIER0_TEST_DEEP_CASES] = {
        ~x,
        (x & 1) != 0 ? x * 7 : x - 5,
        x + 1 < x * 3 ? x ^ 13 : 11,
        tier0_test_deep_floating_sum(x),
        (x + 1000003) / d + x % d + (x << (x & 7)) + x / d + (x >> d),
        (x + 14) + (x + 15),
        tier0_test_deep_floating_sum(x),
        (x + 1000) + (x ^ 0x5555) + x * 77,
        (uint64_t)floating + x - sum,
        x + (x + 15),
        x + (x ^ 3),
    };
    return sum + cases[c];
}

// Whether every callee-saved register (rbx, rbp, r12 to r15) the records write is one the prologue's first records push:
// a register past what selection counted would show here. `writes` has room for `count`.
static bool tier0_test_deep_saved(const X86Instruction* records, uint32_t count, uint64_t* reads, uint64_t* writes)
{
    const uint64_t callee_saved = BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBX) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP) |
                                  BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R12) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R13) |
                                  BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R14) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R15);
    uint64_t pushed = 0;
    uint32_t first = 0;
    // The pushes, and a frame pointer's mov rbp, rsp after the first of them.
    while (first < count && (records[first].form == X86_FORM_PUSH_R64 || (first == 1 && records[first].form == X86_FORM_MOV_RM64_R64 && records[first].rm == X86_REGISTER_RBP)))
    {
        pushed |= records[first].form == X86_FORM_PUSH_R64 ? BUSTER_X86_RESOURCE_GPR(records[first].rm) : 0;
        first += 1;
    }
    x86_instructions_effects_scalar(records, count, reads, writes);
    uint64_t written = 0;
    for (uint32_t r = first; r < count; r += 1)
    {
        written |= writes[r];
    }
    return (written & callee_saved & ~pushed) == 0;
}

static void tier0_test_deep(Tier0TestTally* tally)
{
    static const uint64_t inputs[BUSTER_TIER0_TEST_DEEP_INPUTS] = { 0, 1, 5, 1000, (1ull << 40) + 3, UINT64_MAX - 7 };
    // x, then case 7's (and case 4's and 5's local 1): the saved rsp and the allocations' addresses; case 8's local 1 is
    // (double)(x & 0xffff); cases 9's and 10's local 4 their object.
    static const uint8_t local_classes[5] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID };
    static const uint8_t floating_classes[2] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F64 };
    static const uint8_t local_flags[5] = { 0, 0, 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    static const uint32_t local_sizes[5] = { 0, 0, 0, 0, 16 };
    static const uint8_t alignments_64[5] = { 0, 0, 0, 0, 6 };
    static const uint8_t alignments_4096[5] = { 0, 0, 0, 0, 12 };
    const AbiLocation parameter = { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    const AbiReturn result = { .kind = BUSTER_ABI_RESULT_REGISTERS,
                               .part_bytes = 8,
                               .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE } };
    // Each case's code, in pages of its own, and what each run returned, the child's.
    uint64_t* shared = mmap(0, sizeof(uint64_t) * BUSTER_TIER0_TEST_DEEP_CASES * BUSTER_TIER0_TEST_DEEP_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* region = mmap(0, BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_DEEP_CASES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scratch = malloc(BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE);
    Tier0TestDeep* deep = malloc(sizeof(Tier0TestDeep));
    Arena arena = arena_reserve(1u << 24);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && region != MAP_FAILED && scratch != 0 && deep != 0 && arena.base != 0, "deep", "no memory");
    for (uint32_t c = 0; c < BUSTER_TIER0_TEST_DEEP_CASES && ready; c += 1)
    {
        const char* name = tier0_test_deep_case(c, deep);
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = deep->kinds;
        function.classes = deep->classes;
        function.values = deep->values;
        function.count = deep->count;
        function.local_classes = c == 8 ? floating_classes : local_classes;
        function.local_flags = local_flags;
        function.local_count = c == 9 || c == 10 ? 5 : c == 7 ? 4 : 2;
        function.local_sizes = c == 9 || c == 10 ? local_sizes : 0;
        function.local_alignments = c == 9 ? alignments_64 : c == 10 ? alignments_4096 : 0;
        function.parameter_count = 1;
        function.parameters = &parameter;
        function.result = result;
        const uint64_t mark = arena.length;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        ready = tier0_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "deep",
                                  "%s: rule \"%s\" at %u, error %d at %u, or the paths differ", name, check.rule != 0 ? check.rule : "", check.node, scalar.error,
                                  scalar.node);
        // Every case has deep entries, and only the frame pointer's two and the realigned frames' have push rbp; mov rbp,
        // rsp first, the realigned ones published as such (case 10's with its base).
        const bool pointer = ready && scalar.count >= 2 && scalar.records[0].form == X86_FORM_PUSH_R64 && scalar.records[0].rm == X86_REGISTER_RBP &&
                             scalar.records[1].form == X86_FORM_MOV_RM64_R64 && scalar.records[1].rm == X86_REGISTER_RBP;
        const uint32_t realigned = c == 9 ? 6 : c == 10 ? 12 : 0;
        ready = ready && tier0_test_expect(tally, scalar.deep != 0 && pointer == (c == 5 || c == 7 || c == 9 || c == 10) && scalar.frame_alignment_log2 == realigned &&
                                                      scalar.frame_base == (c == 10),
                                           "deep", "%s: %u deep entries, %s frame pointer, realigned to 2^%u%s", name, scalar.deep, pointer ? "a" : "no",
                                           scalar.frame_alignment_log2, scalar.frame_base ? " with a base" : "");
        uint64_t* reads = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, (uint64_t)scalar.count) : 0;
        uint64_t* writes = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, (uint64_t)scalar.count) : 0;
        ready = ready && tier0_test_expect(tally, reads != 0 && writes != 0 && tier0_test_deep_saved(scalar.records, scalar.count, reads, writes), "deep",
                                           "%s: a callee-saved register written that the prologue doesn't push", name);
        ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.reference_count == 0 &&
                                                      branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                          BRANCH_SIZING_OK,
                                           "deep", "%s: an invalid record, a reference or an invalid branch column", name);
        uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
        const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets)
                                                 : (BranchSizing){ 0 };
        ready = ready && tier0_test_expect(tally, offsets != 0 && sizing.error == BRANCH_SIZING_OK, "deep", "%s: the branches can't be sized", name);
        const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
        uint8_t* page = region + BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE * c;
        const int64_t vector_length = ready && room <= BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE ? x86_encode(scalar.records, scalar.count, page, room, 0, 0) : -1;
        const int64_t scalar_length = ready && room <= BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE ? x86_encode_scalar(scalar.records, scalar.count, scratch, room, 0, 0) : -1;
        ready = ready && tier0_test_expect(tally, vector_length > 0 && vector_length == scalar_length && memcmp(page, scratch, (size_t)vector_length) == 0, "deep",
                                           "%s: the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", name, vector_length, scalar_length);
        arena_pop_to(&arena, mark);
    }
    ready = ready && tier0_test_expect(tally, mprotect(region, BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_DEEP_CASES, PROT_READ | PROT_EXEC) == 0, "deep",
                                       "the code can't be loaded");
    const pid_t child = ready ? fork() : -1;
    tier0_test_expect(tally, !ready || child >= 0, "deep", "fork failed");
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t c = 0; c < BUSTER_TIER0_TEST_DEEP_CASES; c += 1)
        {
            void* address = region + BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE * c;
            uint64_t (*run)(uint64_t) = 0;
            memcpy(&run, &address, sizeof(run));
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_DEEP_INPUTS; v += 1)
            {
                shared[c * BUSTER_TIER0_TEST_DEEP_INPUTS + v] = run(inputs[v]);
            }
        }
        _exit(0);
    }
    if (child > 0)
    {
        int status = 0;
        waitpid(child, &status, 0);
        const bool ran = tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "deep", "the child's status %d", status);
        for (uint32_t c = 0; c < BUSTER_TIER0_TEST_DEEP_CASES && ran; c += 1)
        {
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_DEEP_INPUTS; v += 1)
            {
                const uint64_t expected = tier0_test_deep_model(c, inputs[v]);
                const uint64_t got = shared[c * BUSTER_TIER0_TEST_DEEP_INPUTS + v];
                tier0_test_expect(tally, got == expected, "deep", "%s: x = %" PRIu64 ": %016" PRIx64 ", %016" PRIx64 " expected", tier0_test_deep_case(c, deep),
                                  inputs[v], got, expected);
            }
        }
    }
    arena_release(&arena);
    free(deep);
    free(scratch);
    if (region != MAP_FAILED)
    {
        munmap(region, BUSTER_TIER0_TEST_DEEP_PAGES * BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_DEEP_CASES);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(uint64_t) * BUSTER_TIER0_TEST_DEEP_CASES * BUSTER_TIER0_TEST_DEEP_INPUTS);
    }
}
