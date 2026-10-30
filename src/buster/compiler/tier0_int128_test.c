// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's int128 check: the 128-bit integers (I128 and U128) in Tier 0. Functions over them are compiled by both
// paths, which must give the same records, encoded by both encoders, which must give the same bytes, and run natively in a
// child process, called from C through function pointers, so that this file's C (clang's code of __int128 and unsigned
// __int128) is the oracle of their values and of the calling convention for them:
//   - random programs: statements that each write a value into their 16 bytes of an output (a 128-bit integer of either
//     class, an integer of a class I8 to U64 converted from one, a compare's result), from 128-bit inputs and integers of
//     every class, through constants, arithmetic, shifts by constant and variable counts, ?:, DUP, STORE's kept value and
//     conversions both ways, under live values (an output's address, an integer added after) at the depths of MUL's rax
//     and rdx and a shift's rcx, and through frame objects (LOCAL, STORE_LOCAL and SET_LOCAL of the classes on objects of
//     16 bytes and more, aligned to 8 and to 16), against this file's own stack machine over the same stream
//     (tier0_test_int128_interpret);
//   - boundaries: each 128-bit class's compares and shifts right (and left, by a variable count) on pairs of boundary
//     values, against the interpreter and against C's own operators;
//   - functions of 128-bit parameters and results, which the convention passes as two INTEGER eightbytes in registers or
//     on the stack (Tier 0 takes them as 16-byte objects, as a front end hands them), and functions calling C functions
//     of them, called from C on boundary values;
//   - the memory classes' slots: a stack of their values up to the last slot, which runs, and one past it, which both
//     paths refuse, for the 128-bit integers and for long double;
//   - the stream's rules for the classes, each break refused at its entry.
// tier0_test.c includes this file after tier0_x87_test.c, whose helpers it uses.

// ---------------------------------------------------------------------------------------------------------------------
// The values

#define BUSTER_TIER0_TEST_INT128_PROGRAMS 300
#define BUSTER_TIER0_TEST_INT128_STATEMENTS 8
// The programs' frame objects, locals 3 to 6 after their three pointer parameters, which their last outputs copy out.
#define BUSTER_TIER0_TEST_INT128_OBJECTS 4
#define BUSTER_TIER0_TEST_INT128_OUTPUTS (2 * BUSTER_TIER0_TEST_INT128_STATEMENTS + BUSTER_TIER0_TEST_INT128_OBJECTS)
#define BUSTER_TIER0_TEST_INT128_IN 8
#define BUSTER_TIER0_TEST_INT128_INTS 16
#define BUSTER_TIER0_TEST_INT128_ENTRIES 4096
#define BUSTER_TIER0_TEST_INT128_TASKS 512
#define BUSTER_TIER0_TEST_INT128_SLOTS 128

typedef unsigned __int128 Tier0TestWide;

static const uint8_t tier0_test_int128_integers[8] = {
    BUSTER_TYPED_CLASS_I8, BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_I16, BUSTER_TYPED_CLASS_U16,
    BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64,
};

static Tier0TestWide tier0_test_int128_make(uint64_t high, uint64_t low)
{
    return (Tier0TestWide)high << 64 | low;
}

// A random 64-bit value: now and then one at a boundary (0, 1, -1, the extremes of 32 and 64 bits, a power of two), else
// random bits.
static uint64_t tier0_test_int128_random64(uint64_t* state)
{
    static const uint64_t boundaries[12] = {
        0, 1, ~0ull, 0x7fffffffffffffffull, 0x8000000000000000ull, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0xffffffff80000000ull, 0x100000000ull, 2, 0x5555555555555555ull,
    };
    const uint32_t shape = tier0_generate_below(state, 3);
    return shape == 0 ? boundaries[tier0_generate_below(state, 12)] : tier0_generate_random(state);
}

// A random 128-bit value: its halves random or at their boundaries, now and then a 64-bit value sign- or zero-extended.
static Tier0TestWide tier0_test_int128_random(uint64_t* state)
{
    const uint64_t low = tier0_test_int128_random64(state);
    const uint32_t shape = tier0_generate_below(state, 4);
    const uint64_t high = shape == 0 ? 0 : shape == 1 ? (uint64_t)((int64_t)low >> 63) : tier0_test_int128_random64(state);
    return tier0_test_int128_make(high, low);
}

// The C functions the hand-written functions call, as Tier 0's code calls them (through their addresses).
static __int128 tier0_test_int128_twice(__int128 x, int64_t k)
{
    return (__int128)((Tier0TestWide)x * 2 + (Tier0TestWide)(__int128)k);
}

static unsigned __int128 tier0_test_int128_stacked(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, unsigned __int128 x)
{
    return x * 7 - (Tier0TestWide)(__int128)(a + 2 * b + 3 * c + 4 * d + 5 * e);
}

// ---------------------------------------------------------------------------------------------------------------------
// The signatures

// The signatures of the check's functions and calls, and their locations from one abi_assign batch:
//   - MULADD: __int128 (__int128 a, unsigned __int128 b, long c), its parameters in rdi:rsi, rdx:rcx and r8;
//   - STACKED: unsigned __int128 (long a, long b, long c, long d, long e, unsigned __int128 x), x on the stack (no pair
//     of registers is left for it);
//   - TWICE: __int128 (__int128 x, long k), and CALLER: __int128 (__int128 a), which calls it and STACKED;
//   - BODY: void (void* in, void* ints, void* out), the programs'.
#define BUSTER_TIER0_TEST_INT128_MULADD 0
#define BUSTER_TIER0_TEST_INT128_STACKED 1
#define BUSTER_TIER0_TEST_INT128_TWICE 2
#define BUSTER_TIER0_TEST_INT128_CALLER 3
#define BUSTER_TIER0_TEST_INT128_BODY 4
#define BUSTER_TIER0_TEST_INT128_SIGNATURES 5

typedef struct Tier0TestInt128Abi Tier0TestInt128Abi;
struct Tier0TestInt128Abi
{
    AbiLocation locations[24];
    uint32_t first[BUSTER_TIER0_TEST_INT128_SIGNATURES];
    AbiCall calls[BUSTER_TIER0_TEST_INT128_SIGNATURES];
};

static bool tier0_test_int128_abi(Tier0Tables* tables, Tier0TestInt128Abi* abi)
{
    const TypeId s = BUSTER_TYPE_INT128;
    const TypeId u = BUSTER_TYPE_UINT128;
    const TypeId l = BUSTER_TYPE_LONG;
    const TypeId p = BUSTER_TYPE_ULONG;
    bool valid = true;
    {
        abi_classify_all(&tables->abi, &tables->types);
        const TypeId results[BUSTER_TIER0_TEST_INT128_SIGNATURES] = { s, u, s, s, BUSTER_TYPE_VOID };
        uint32_t counts[BUSTER_TIER0_TEST_INT128_SIGNATURES] = { 3, 6, 2, 1, 3 };
        uint32_t named[BUSTER_TIER0_TEST_INT128_SIGNATURES] = { 3, 6, 2, 1, 3 };
        uint8_t variadic[BUSTER_TIER0_TEST_INT128_SIGNATURES] = { 0, 0, 0, 0, 0 };
        const TypeId arguments[] = {
            s, u, l,
            l, l, l, l, l, u,
            s, l,
            s,
            p, p, p,
        };
        uint32_t at = 0;
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_SIGNATURES; k += 1)
        {
            abi->first[k] = at;
            at += counts[k];
        }
        const AbiSignatures batch = {
            .results = results,
            .counts = counts,
            .named = named,
            .variadic = variadic,
            .count = BUSTER_TIER0_TEST_INT128_SIGNATURES,
            .arguments = arguments,
        };
        abi_assign(&tables->abi, batch, abi->locations, abi->calls);
        valid = at == BUSTER_ARRAY_LENGTH(arguments) && at <= BUSTER_ARRAY_LENGTH(abi->locations);
    }
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// A program's stream and what each statement wrote (its class, or VOID for a slot it left).
typedef struct Tier0TestInt128Program Tier0TestInt128Program;
struct Tier0TestInt128Program
{
    uint8_t kinds[BUSTER_TIER0_TEST_INT128_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_INT128_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_INT128_ENTRIES];
    uint32_t count;
    uint8_t outputs[BUSTER_TIER0_TEST_INT128_OUTPUTS];
    // Each ?:'s branch and jump, whose targets its labels patch.
    uint32_t patches[BUSTER_TIER0_TEST_INT128_SLOTS];
    uint32_t patch_count;
    bool overflow;
};

static void tier0_test_int128_emit(Tier0TestInt128Program* program, uint32_t kind, uint32_t class, uint64_t value)
{
    if (program->count < BUSTER_TIER0_TEST_INT128_ENTRIES)
    {
        program->kinds[program->count] = (uint8_t)kind;
        program->classes[program->count] = (uint8_t)class;
        program->values[program->count] = value;
        program->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

// The address of element `index` of parameter `parameter`'s array, of `size`-byte elements.
static void tier0_test_int128_element(Tier0TestInt128Program* program, uint32_t parameter, uint32_t index, uint32_t size)
{
    tier0_test_int128_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, parameter);
    tier0_test_int128_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, index);
    tier0_test_int128_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, size);
}

// The generator's tasks, popped from a stack: an expression to make (a 128-bit integer's of `class`, of a budget of
// nodes), an entry to emit once its operands are, a shift's count, and ?:'s branch, jump and labels (`value` its patch).
#define BUSTER_TIER0_TEST_INT128_TASK_EXPRESSION 0
#define BUSTER_TIER0_TEST_INT128_TASK_ENTRY 1
#define BUSTER_TIER0_TEST_INT128_TASK_COUNT 2
#define BUSTER_TIER0_TEST_INT128_TASK_BRANCH 3
#define BUSTER_TIER0_TEST_INT128_TASK_JUMP 4
#define BUSTER_TIER0_TEST_INT128_TASK_LABEL 5

typedef struct Tier0TestInt128Task Tier0TestInt128Task;
struct Tier0TestInt128Task
{
    uint8_t task;
    uint8_t kind;
    uint8_t class;
    uint32_t budget;
    uint64_t value;
};

typedef struct Tier0TestInt128Tasks Tier0TestInt128Tasks;
struct Tier0TestInt128Tasks
{
    Tier0TestInt128Task items[BUSTER_TIER0_TEST_INT128_TASKS];
    uint32_t count;
};

static void tier0_test_int128_push(Tier0TestInt128Program* program, Tier0TestInt128Tasks* tasks, Tier0TestInt128Task task)
{
    if (tasks->count < BUSTER_TIER0_TEST_INT128_TASKS)
    {
        tasks->items[tasks->count] = task;
        tasks->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

static Tier0TestInt128Task tier0_test_int128_entry(uint32_t kind, uint32_t class, uint64_t value)
{
    return (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_ENTRY, .kind = (uint8_t)kind, .class = (uint8_t)class, .value = value };
}

static Tier0TestInt128Task tier0_test_int128_expression(uint32_t class, uint32_t budget)
{
    return (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_EXPRESSION, .class = (uint8_t)class, .budget = budget };
}

// The other 128-bit class.
static uint32_t tier0_test_int128_other(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_I128 ? BUSTER_TYPED_CLASS_U128 : BUSTER_TYPED_CLASS_I128;
}

// A leaf of `class`: an input, a constant (a low half past a sign-extended imm32 now and then), an integer of any class
// converted, an input of the other class converted, or a frame object's value (whichever class wrote it: its 16 bytes).
static void tier0_test_int128_leaf(Tier0TestInt128Program* program, uint32_t class, uint64_t* state)
{
    const uint32_t choice = tier0_generate_below(state, 10);
    if (choice < 3)
    {
        tier0_test_int128_element(program, 0, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_IN), 16);
        tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
    }
    else if (choice < 5)
    {
        tier0_test_int128_emit(program, BUSTER_TYPED_CONST, class, tier0_test_int128_random64(state));
    }
    else if (choice < 7)
    {
        tier0_test_int128_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_INTS), 8);
        tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, tier0_test_int128_integers[tier0_generate_below(state, 8)], 0);
        tier0_test_int128_emit(program, BUSTER_TYPED_CONVERT, class, 0);
    }
    else if (choice < 8)
    {
        tier0_test_int128_element(program, 0, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_IN), 16);
        tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, tier0_test_int128_other(class), 0);
        tier0_test_int128_emit(program, BUSTER_TYPED_CONVERT, class, 0);
    }
    else
    {
        tier0_test_int128_emit(program, BUSTER_TYPED_LOCAL, class, 3 + tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_OBJECTS));
    }
}

// Runs the task stack until it is empty: each expression becomes a leaf (its budget spent, or by chance) or an operation
// whose operands are new tasks, pushed after it in reverse, so that they come out in postorder.
static void tier0_test_int128_run_tasks(Tier0TestInt128Program* program, Tier0TestInt128Tasks* tasks, uint64_t* state)
{
    while (tasks->count != 0 && !program->overflow)
    {
        tasks->count -= 1;
        const Tier0TestInt128Task task = tasks->items[tasks->count];
        if (task.task == BUSTER_TIER0_TEST_INT128_TASK_ENTRY)
        {
            tier0_test_int128_emit(program, task.kind, task.class, task.value);
        }
        else if (task.task == BUSTER_TIER0_TEST_INT128_TASK_COUNT)
        {
            // A shift's count below 128: a constant, or an integer's low 7 bits of a class the generator picks.
            if (tier0_generate_chance(state, 50))
            {
                static const uint8_t counts[10] = { 0, 1, 31, 63, 64, 65, 96, 127, 32, 100 };
                tier0_test_int128_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, counts[tier0_generate_below(state, 10)]);
            }
            else
            {
                const uint32_t class = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
                tier0_test_int128_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_INTS), 8);
                tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
                tier0_test_int128_emit(program, BUSTER_TYPED_CONST, class, 127);
                tier0_test_int128_emit(program, BUSTER_TYPED_AND, class, 0);
            }
        }
        else if (task.task == BUSTER_TIER0_TEST_INT128_TASK_BRANCH || task.task == BUSTER_TIER0_TEST_INT128_TASK_JUMP)
        {
            program->patches[task.value] = program->count;
            tier0_test_int128_emit(program, task.task == BUSTER_TIER0_TEST_INT128_TASK_BRANCH ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_JUMP_VALUE,
                                   task.task == BUSTER_TIER0_TEST_INT128_TASK_BRANCH ? BUSTER_TYPED_CLASS_I32 : task.class, 0);
        }
        else if (task.task == BUSTER_TIER0_TEST_INT128_TASK_LABEL)
        {
            program->values[program->patches[task.value]] = program->count;
        }
        else if (task.budget <= 1 || tier0_generate_chance(state, 20))
        {
            tier0_test_int128_leaf(program, task.class, state);
        }
        else
        {
            const uint32_t class = task.class;
            const uint32_t choice = tier0_generate_below(state, 100);
            const uint32_t budget = task.budget - 1;
            const uint32_t left = 1 + tier0_generate_below(state, budget > 1 ? budget - 1 : 1);
            const uint32_t right = budget > left ? budget - left : 1;
            if (choice < 40)
            {
                static const uint8_t operations[6] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_AND, BUSTER_TYPED_OR, BUSTER_TYPED_XOR };
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(operations[tier0_generate_below(state, 6)], class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, right));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, left));
            }
            else if (choice < 48)
            {
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(tier0_generate_chance(state, 50) ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, budget));
            }
            else if (choice < 54)
            {
                // x * x or x + x, by DUP.
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(tier0_generate_chance(state, 50) ? BUSTER_TYPED_MUL : BUSTER_TYPED_ADD, class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(BUSTER_TYPED_DUP, class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, budget));
            }
            else if (choice < 68)
            {
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(tier0_generate_chance(state, 50) ? BUSTER_TYPED_SHL : BUSTER_TYPED_SHR, class, 0));
                tier0_test_int128_push(program, tasks, (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_COUNT });
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, budget));
            }
            else if (choice < 80 && program->patch_count + 2 <= BUSTER_TIER0_TEST_INT128_SLOTS && budget >= 4)
            {
                // (a OP b) ? x : y, the compare's operands of either class, the arms' value carried by JUMP_VALUE.
                const uint32_t branch = program->patch_count;
                const uint32_t jump = program->patch_count + 1;
                program->patch_count += 2;
                const uint32_t compare = BUSTER_TYPED_EQ + tier0_generate_below(state, 6);
                const uint32_t compared = tier0_generate_chance(state, 50) ? class : tier0_test_int128_other(class);
                const uint32_t quarter = budget / 4 != 0 ? budget / 4 : 1;
                tier0_test_int128_push(program, tasks, (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_LABEL, .value = jump });
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, quarter));
                tier0_test_int128_push(program, tasks, (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_LABEL, .value = branch });
                tier0_test_int128_push(program, tasks, (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_JUMP, .class = (uint8_t)class, .value = jump });
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, quarter));
                tier0_test_int128_push(program, tasks, (Tier0TestInt128Task){ .task = BUSTER_TIER0_TEST_INT128_TASK_BRANCH, .value = branch });
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(compare, BUSTER_TYPED_CLASS_I32, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(compared, quarter));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(compared, quarter));
            }
            else if (choice < 86)
            {
                // The value stored into a frame object, and kept (STORE_LOCAL).
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(BUSTER_TYPED_STORE_LOCAL, class, 3 + tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_OBJECTS)));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, budget));
            }
            else if (choice < 93)
            {
                // Through an integer class and back: its low bits, extended by its signedness.
                const uint32_t through = tier0_test_int128_integers[tier0_generate_below(state, 8)];
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(BUSTER_TYPED_CONVERT, class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(BUSTER_TYPED_CONVERT, through, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(class, budget));
            }
            else
            {
                // The other class's expression, converted.
                tier0_test_int128_push(program, tasks, tier0_test_int128_entry(BUSTER_TYPED_CONVERT, class, 0));
                tier0_test_int128_push(program, tasks, tier0_test_int128_expression(tier0_test_int128_other(class), budget));
            }
        }
    }
}

// A program: its frame objects given values first (a constant's or an input's, by SET_LOCAL), each statement into its
// output slots (2s and 2s + 1, 16 bytes each) or a frame object, then each object copied out into its own output slot
// (after the statements'), then RETURN.
static void tier0_test_int128_program(Tier0TestInt128Program* program, uint64_t* state)
{
    memset(program, 0, sizeof(*program));
    const uint32_t statements = 1 + tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_STATEMENTS);
    Tier0TestInt128Tasks tasks;
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_OBJECTS; k += 1)
    {
        const uint32_t class = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128;
        if (tier0_generate_chance(state, 50))
        {
            tier0_test_int128_emit(program, BUSTER_TYPED_CONST, class, tier0_test_int128_random64(state));
        }
        else
        {
            tier0_test_int128_element(program, 0, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_IN), 16);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
        }
        tier0_test_int128_emit(program, BUSTER_TYPED_SET_LOCAL, class, 3 + k);
    }
    for (uint32_t s = 0; s < statements && !program->overflow; s += 1)
    {
        const uint32_t root = tier0_generate_below(state, 11);
        const uint32_t budget = 2 + tier0_generate_below(state, 11);
        const uint32_t class = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128;
        tasks.count = 0;
        tier0_test_int128_element(program, 2, 2 * s, 16);
        if (root < 3)
        {
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET, class, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget));
            program->outputs[2 * s] = (uint8_t)class;
        }
        else if (root < 5)
        {
            // out[2s] = (out[2s + 1] = x): STORE's kept value.
            tier0_test_int128_element(program, 2, 2 * s + 1, 16);
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET, class, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_STORE, class, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget));
            program->outputs[2 * s] = (uint8_t)class;
            program->outputs[2 * s + 1] = (uint8_t)class;
        }
        else if (root < 7)
        {
            // An integer of a class, from the expression's low bits.
            const uint32_t to = tier0_test_int128_integers[tier0_generate_below(state, 8)];
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET, to, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_CONVERT, to, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget));
            program->outputs[2 * s] = (uint8_t)to;
        }
        else if (root < 8)
        {
            // An integer loaded first, under the expression (in rcx, depth 1's register, with the output's address in rax),
            // added to its low half: values the fixed registers' saves must keep.
            tier0_test_int128_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_INTS), 8);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget));
            program->outputs[2 * s] = BUSTER_TYPED_CLASS_I64;
        }
        else if (root < 10)
        {
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I32, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_EQ + tier0_generate_below(state, 6), BUSTER_TYPED_CLASS_I32, 0));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget / 2 + 1));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget / 2 + 1));
            program->outputs[2 * s] = BUSTER_TYPED_CLASS_I32;
        }
        else
        {
            // A frame object's new value (SET_LOCAL), which the statements after it and its own output slot read: the
            // output's address goes unused.
            tier0_test_int128_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_push(program, &tasks, tier0_test_int128_entry(BUSTER_TYPED_SET_LOCAL, class, 3 + tier0_generate_below(state, BUSTER_TIER0_TEST_INT128_OBJECTS)));
            tier0_test_int128_push(program, &tasks, tier0_test_int128_expression(class, budget));
        }
        tier0_test_int128_run_tasks(program, &tasks, state);
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_OBJECTS; k += 1)
    {
        tier0_test_int128_element(program, 2, 2 * BUSTER_TIER0_TEST_INT128_STATEMENTS + k, 16);
        tier0_test_int128_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U128, 3 + k);
        tier0_test_int128_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U128, 0);
        program->outputs[2 * BUSTER_TIER0_TEST_INT128_STATEMENTS + k] = BUSTER_TYPED_CLASS_U128;
    }
    tier0_test_int128_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

// Whether a function has 128-bit integer values (Tier0Function.int128), as its producer knows: an entry of class I128 or
// U128.
static bool tier0_test_int128_has(const Tier0Function* function)
{
    return memchr(function->classes, BUSTER_TYPED_CLASS_I128, function->count) != 0 || memchr(function->classes, BUSTER_TYPED_CLASS_U128, function->count) != 0;
}

// The program as Tier 0 takes it: three pointer parameters (the 128-bit inputs, the integers and the output) in their
// registers, and its frame objects, of 16, 24, 16 and 32 bytes aligned to 16, 8, 8 and 16.
static Tier0Function tier0_test_int128_function(const Tier0TestInt128Program* program, const Tier0TestInt128Abi* abi)
{
    static const uint8_t local_classes[3 + BUSTER_TIER0_TEST_INT128_OBJECTS] = {
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID,
    };
    static const uint8_t local_flags[3 + BUSTER_TIER0_TEST_INT128_OBJECTS] = {
        0, 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    };
    static const uint32_t local_sizes[3 + BUSTER_TIER0_TEST_INT128_OBJECTS] = { 0, 0, 0, 16, 24, 16, 32 };
    static const uint8_t local_alignments[3 + BUSTER_TIER0_TEST_INT128_OBJECTS] = { 0, 0, 0, 4, 3, 3, 4 };
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = program->kinds;
    function.classes = program->classes;
    function.values = program->values;
    function.count = program->count;
    function.local_classes = local_classes;
    function.local_flags = local_flags;
    function.local_sizes = local_sizes;
    function.local_alignments = local_alignments;
    function.local_count = 3 + BUSTER_TIER0_TEST_INT128_OBJECTS;
    function.parameter_count = 3;
    function.parameters = abi->locations + abi->first[BUSTER_TIER0_TEST_INT128_BODY];
    function.result = abi->calls[BUSTER_TIER0_TEST_INT128_BODY].result;
    function.int128 = tier0_test_int128_has(&function);
    return function;
}

// ---------------------------------------------------------------------------------------------------------------------
// The interpreter

// A value of the stack machine: a 128-bit integer's, or an integer's bits as its class means them (sign- or
// zero-extended to 64).
typedef struct Tier0TestInt128Value Tier0TestInt128Value;
struct Tier0TestInt128Value
{
    Tier0TestWide wide;
    uint64_t bits;
};

static bool tier0_test_int128_compare(uint32_t kind, bool is_signed, Tier0TestWide a, Tier0TestWide b)
{
    const bool less = is_signed ? (__int128)a < (__int128)b : a < b;
    const bool equal = a == b;
    return kind == BUSTER_TYPED_EQ ? equal : kind == BUSTER_TYPED_NE ? !equal : kind == BUSTER_TYPED_LT ? less : kind == BUSTER_TYPED_LE ? less || equal
           : kind == BUSTER_TYPED_GT ? !less && !equal : !less;
}

// What the interpreter marks in `visited` (an entry each): the entries it ran, and for a BRANCH_FALSE whether it was
// taken (to ?:'s second arm) or not (to its first).
#define BUSTER_TIER0_TEST_INT128_VISITED_RAN 1
#define BUSTER_TIER0_TEST_INT128_VISITED_TAKEN 2
#define BUSTER_TIER0_TEST_INT128_VISITED_NOT_TAKEN 4

// The program's stream run as a stack machine on `parameters`, as C means it: unsigned __int128's wrapping arithmetic,
// __int128's signed compares and arithmetic right shifts, marking in `visited` (cleared, an entry each) what it ran.
// Returns whether it ran to its RETURN.
static bool tier0_test_int128_interpret(const Tier0TestInt128Program* program, const uint64_t parameters[3], uint8_t* visited)
{
    Tier0TestWide objects[BUSTER_TIER0_TEST_INT128_OBJECTS] = { 0 };
    memset(visited, 0, program->count);
    Tier0TestInt128Value stack[64];
    uint32_t depth = 0;
    uint32_t i = 0;
    bool valid = true;
    bool done = false;
    while (valid && !done && i < program->count)
    {
        const uint32_t kind = program->kinds[i];
        const uint32_t class = program->classes[i];
        const uint64_t value = program->values[i];
        const uint32_t operand = i > 0 ? program->classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
        const bool wide = tier0_class_int128(class);
        uint32_t next = i + 1;
        valid = depth + 1 < BUSTER_ARRAY_LENGTH(stack);
        visited[i] |= BUSTER_TIER0_TEST_INT128_VISITED_RAN;
        if (kind == BUSTER_TYPED_CONST && wide)
        {
            stack[depth].wide = class == BUSTER_TYPED_CLASS_I128 ? (Tier0TestWide)(__int128)(int64_t)value : (Tier0TestWide)value;
            depth += 1;
        }
        else if (kind == BUSTER_TYPED_LOCAL && wide)
        {
            stack[depth].wide = objects[value - 3];
            depth += 1;
        }
        else if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            objects[value - 3] = stack[depth - 1].wide;
            depth -= kind == BUSTER_TYPED_SET_LOCAL ? 1 : 0;
        }
        else if (kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_LOCAL)
        {
            stack[depth].bits = kind == BUSTER_TYPED_LOCAL ? parameters[value] : tier0_test_x87_integer(class, value);
            depth += 1;
        }
        else if (kind == BUSTER_TYPED_PTR_ADD)
        {
            stack[depth - 2].bits += stack[depth - 1].bits * value;
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_LOAD)
        {
            const uint8_t* address = (const uint8_t*)(uintptr_t)stack[depth - 1].bits;
            uint64_t bits = 0;
            if (wide)
            {
                memcpy(&stack[depth - 1].wide, address, 16);
            }
            else
            {
                memcpy(&bits, address, tier0_class_bytes(class));
                stack[depth - 1].bits = tier0_test_x87_integer(class, bits);
            }
        }
        else if (kind == BUSTER_TYPED_CONVERT)
        {
            Tier0TestInt128Value* top = &stack[depth - 1];
            if (wide && !tier0_class_int128(operand))
            {
                top->wide = tier0_class_signed(operand) ? (Tier0TestWide)(__int128)(int64_t)top->bits : (Tier0TestWide)top->bits;
            }
            else if (!wide)
            {
                top->bits = tier0_test_x87_integer(class, tier0_class_int128(operand) ? (uint64_t)top->wide : top->bits);
            }
        }
        else if (kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT)
        {
            stack[depth - 1].wide = kind == BUSTER_TYPED_NEG ? 0 - stack[depth - 1].wide : ~stack[depth - 1].wide;
        }
        else if ((kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_XOR && kind != BUSTER_TYPED_DIV && kind != BUSTER_TYPED_MOD) ||
                 kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR)
        {
            Tier0TestInt128Value* a = &stack[depth - 2];
            const Tier0TestInt128Value* b = &stack[depth - 1];
            if (wide && (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR))
            {
                const uint32_t count = (uint32_t)(b->bits & 127);
                a->wide = kind == BUSTER_TYPED_SHL ? a->wide << count : class == BUSTER_TYPED_CLASS_I128 ? (Tier0TestWide)((__int128)a->wide >> count) : a->wide >> count;
            }
            else if (wide)
            {
                a->wide = kind == BUSTER_TYPED_ADD ? a->wide + b->wide : kind == BUSTER_TYPED_SUB ? a->wide - b->wide : kind == BUSTER_TYPED_MUL ? a->wide * b->wide
                          : kind == BUSTER_TYPED_AND ? a->wide & b->wide : kind == BUSTER_TYPED_OR ? a->wide | b->wide : a->wide ^ b->wide;
            }
            else
            {
                // The integers' AND (a count's mask) and ADD.
                a->bits = tier0_test_x87_integer(class, kind == BUSTER_TYPED_AND ? a->bits & b->bits : a->bits + b->bits);
            }
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_DUP)
        {
            stack[depth] = stack[depth - 1];
            depth += 1;
        }
        else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
        {
            stack[depth - 2].bits = tier0_test_int128_compare(kind, operand == BUSTER_TYPED_CLASS_I128, stack[depth - 2].wide, stack[depth - 1].wide) ? 1 : 0;
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_BRANCH_FALSE)
        {
            depth -= 1;
            next = stack[depth].bits == 0 ? (uint32_t)value : next;
            visited[i] |= stack[depth].bits == 0 ? BUSTER_TIER0_TEST_INT128_VISITED_TAKEN : BUSTER_TIER0_TEST_INT128_VISITED_NOT_TAKEN;
        }
        else if (kind == BUSTER_TYPED_JUMP_VALUE)
        {
            next = (uint32_t)value;
        }
        else if (kind == BUSTER_TYPED_DISCARD)
        {
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_SET || kind == BUSTER_TYPED_STORE)
        {
            uint8_t* address = (uint8_t*)(uintptr_t)stack[depth - 2].bits;
            memcpy(address, wide ? (const void*)&stack[depth - 1].wide : (const void*)&stack[depth - 1].bits, wide ? 16 : tier0_class_bytes(class));
            stack[depth - 2] = stack[depth - 1];
            depth -= kind == BUSTER_TYPED_STORE ? 1 : 2;
        }
        else if (kind == BUSTER_TYPED_RETURN)
        {
            done = true;
        }
        else
        {
            valid = false;
        }
        i = next;
    }
    return valid && done;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compiling and running

// What the native runs share with the test: the inputs, and per run what the code wrote and returned.
typedef struct Tier0TestInt128Shared Tier0TestInt128Shared;
struct Tier0TestInt128Shared
{
    Tier0TestWide in[BUSTER_TIER0_TEST_INT128_IN];
    uint64_t ints[BUSTER_TIER0_TEST_INT128_INTS];
    uint8_t out[BUSTER_TIER0_TEST_INT128_OUTPUTS * 16];
    Tier0TestWide arguments[16][2];
    Tier0TestWide results[16];
};

typedef void (*Tier0TestInt128Run)(const void* code, Tier0TestInt128Shared* shared);

// Runs `run` on `code` (`length` bytes) in a child, which must exit with status 0: the code mapped executable at a page of
// its own, `run` calling it from C.
static bool tier0_test_int128_native(const uint8_t* code, uint32_t length, Tier0TestInt128Run run, Tier0TestInt128Shared* shared)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        uint8_t* page = mmap(0, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
        {
            _exit(3);
        }
        memcpy(page, code, length);
        if (mprotect(page, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        run(page, shared);
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_int128_run_body(const void* code, Tier0TestInt128Shared* shared)
{
    typedef void (*Body)(const void*, const void*, void*);
    Body body = 0;
    memcpy(&body, &code, sizeof(body));
    body(shared->in, shared->ints, shared->out);
}

// What the programs reach when they run, which the default count must cover whole: only the entries the interpreter ran
// count (of programs whose native run matched it), not a ?:'s arm it skipped. For each 128-bit class apart (index 0
// I128, 1 U128: their signed and unsigned code differs), in bitmaps of their own so that no bit of one stands for
// another's: each kind of entry of the class (CONST, DUP, NEG, NOT, LOAD, STORE, SET, ADD to XOR but division, the
// shifts, JUMP_VALUE, LOCAL, STORE_LOCAL and SET_LOCAL on frame objects, and each compare of two values of it), each
// conversion to it by its source class and from it by its destination class, MUL's and the shifts' saves of rax, rdx and
// rcx, and both arms of a ?: of the class.
typedef struct Tier0TestInt128Reach Tier0TestInt128Reach;
struct Tier0TestInt128Reach
{
    // A bit per kind, below BUSTER_TYPED_KIND_LIMIT.
    uint64_t kinds[2];
    // A bit per class, below BUSTER_TYPED_CLASS_LIMIT: converted to the 128-bit class (its source), and from it (its
    // destination).
    uint16_t to[2];
    uint16_t from[2];
    // A bit per fixed register saved, BUSTER_TIER0_TEST_INT128_SAVE_*.
    uint8_t saves[2];
    // A bit per arm of a ?: run, BUSTER_TIER0_TEST_INT128_ARM_*.
    uint8_t arms[2];
};

#define BUSTER_TIER0_TEST_INT128_SAVE_RAX 1
#define BUSTER_TIER0_TEST_INT128_SAVE_RDX 2
#define BUSTER_TIER0_TEST_INT128_SAVE_RCX 4
#define BUSTER_TIER0_TEST_INT128_SAVES 7

#define BUSTER_TIER0_TEST_INT128_ARM_FIRST 1
#define BUSTER_TIER0_TEST_INT128_ARM_SECOND 2
#define BUSTER_TIER0_TEST_INT128_ARMS 3

// A 128-bit class's index in Tier0TestInt128Reach's arrays.
static uint32_t tier0_test_int128_index(uint32_t class)
{
    return class == BUSTER_TYPED_CLASS_U128 ? 1 : 0;
}

// What a program reached, from `visited` (tier0_test_int128_interpret's marks).
static Tier0TestInt128Reach tier0_test_int128_reached(const Tier0TestInt128Program* program, const uint8_t* visited)
{
    Tier0TestInt128Reach reached = { 0 };
    int32_t depth = 0;
    for (uint32_t i = 0; i < program->count; i += 1)
    {
        const bool ran = (visited[i] & BUSTER_TIER0_TEST_INT128_VISITED_RAN) != 0;
        const uint32_t kind = program->kinds[i];
        const uint32_t class = program->classes[i];
        const uint32_t operand = i > 0 ? program->classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
        const bool compare = kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE && tier0_class_int128(operand);
        const bool wide = ran && (tier0_class_int128(class) || compare);
        // The 128-bit class the entry's code is for: a compare's operands', or its own.
        const uint32_t index = tier0_test_int128_index(compare ? operand : class);
        if (ran && kind == BUSTER_TYPED_CONVERT && tier0_class_int128(class))
        {
            reached.to[index] |= (uint16_t)(1u << operand);
        }
        else if (ran && kind == BUSTER_TYPED_CONVERT && tier0_class_int128(operand))
        {
            reached.from[tier0_test_int128_index(operand)] |= (uint16_t)(1u << class);
        }
        else if (wide)
        {
            reached.kinds[index] |= 1ull << kind;
        }
        // The integer stack's depth before the entry, by the stream's own effects (no fusion: the programs' 128-bit
        // entries absorb nothing, nor does the integer that sits under them).
        if (wide && kind == BUSTER_TYPED_MUL)
        {
            reached.saves[index] |= depth > 2 ? BUSTER_TIER0_TEST_INT128_SAVE_RAX : 0;
            reached.saves[index] |= depth > 4 ? BUSTER_TIER0_TEST_INT128_SAVE_RDX : 0;
        }
        if (wide && (kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR))
        {
            reached.saves[index] |= depth > 3 ? BUSTER_TIER0_TEST_INT128_SAVE_RCX : 0;
        }
        // A ?:'s branch: its first arm ends at the JUMP_VALUE before the second's start, the branch's target, whose class
        // is the ?:'s.
        if (ran && kind == BUSTER_TYPED_BRANCH_FALSE && program->values[i] > 0 && program->values[i] <= program->count &&
            program->kinds[program->values[i] - 1] == BUSTER_TYPED_JUMP_VALUE && tier0_class_int128(program->classes[program->values[i] - 1]))
        {
            const uint32_t arm = tier0_test_int128_index(program->classes[program->values[i] - 1]);
            reached.arms[arm] |= (visited[i] & BUSTER_TIER0_TEST_INT128_VISITED_NOT_TAKEN) != 0 ? BUSTER_TIER0_TEST_INT128_ARM_FIRST : 0;
            reached.arms[arm] |= (visited[i] & BUSTER_TIER0_TEST_INT128_VISITED_TAKEN) != 0 ? BUSTER_TIER0_TEST_INT128_ARM_SECOND : 0;
        }
        const Tier0Shape shape = tier0_shapes[kind];
        const bool returns_nothing = kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID;
        depth -= shape.first != BUSTER_TIER0_STACK_NONE ? 1 : 0;
        depth -= shape.last == BUSTER_TIER0_STACK_OPERAND && !returns_nothing ? 1 : 0;
        depth += shape.push != BUSTER_TIER0_STACK_NONE ? 1 : 0;
    }
    return reached;
}

// Every kind, conversion and save the programs are meant to reach, for each class (tier0_test_int128_reached's bits).
static Tier0TestInt128Reach tier0_test_int128_wanted(void)
{
    static const uint8_t kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_NEG, BUSTER_TYPED_NOT, BUSTER_TYPED_LOAD, BUSTER_TYPED_STORE, BUSTER_TYPED_SET,
        BUSTER_TYPED_ADD,   BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_AND, BUSTER_TYPED_OR,   BUSTER_TYPED_XOR,   BUSTER_TYPED_SHL,
        BUSTER_TYPED_SHR,   BUSTER_TYPED_EQ,  BUSTER_TYPED_NE,  BUSTER_TYPED_LT,  BUSTER_TYPED_LE,   BUSTER_TYPED_GT,    BUSTER_TYPED_GE,
        BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_SET_LOCAL,
    };
    Tier0TestInt128Reach wanted = { 0 };
    for (uint32_t index = 0; index < 2; index += 1)
    {
        const uint32_t class = index == 0 ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128;
        wanted.saves[index] = BUSTER_TIER0_TEST_INT128_SAVES;
        wanted.arms[index] = BUSTER_TIER0_TEST_INT128_ARMS;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
        {
            wanted.kinds[index] |= 1ull << kinds[k];
        }
        for (uint32_t integer = BUSTER_TYPED_CLASS_I8; integer <= BUSTER_TYPED_CLASS_U64; integer += 1)
        {
            wanted.to[index] |= (uint16_t)(1u << integer);
            wanted.from[index] |= (uint16_t)(1u << integer);
        }
        wanted.to[index] |= (uint16_t)(1u << tier0_test_int128_other(class));
    }
    return wanted;
}

// Adds what one checked program reached.
static void tier0_test_int128_reach(Tier0TestInt128Reach* reached, const Tier0TestInt128Reach* program)
{
    for (uint32_t index = 0; index < 2; index += 1)
    {
        reached->kinds[index] |= program->kinds[index];
        reached->to[index] |= program->to[index];
        reached->from[index] |= program->from[index];
        reached->saves[index] |= program->saves[index];
        reached->arms[index] |= program->arms[index];
    }
}

// A function compiled by both paths, which must make the same code, its records valid, its branches sized, and encoded by
// both encoders into `image` (the vector one's) and `scratch` (the scalar one's), which must be the same bytes. Returns
// the code's length, 0 for a failure, and in *error Tier 0's error. Any error is a failure but, with `depth_allowed`, a
// depth past the slots, which both paths give alike for a random program that stacks too many values.
static uint32_t tier0_test_int128_encode(Tier0TestTally* tally, const char* name, const Tier0Function* function, Arena* arena, uint8_t* image, uint8_t* scratch,
                                         bool depth_allowed, Tier0Error* error)
{
    const Tier0Check check = tier0_validate(function);
    const Tier0Code scalar = tier0_compile_scalar(function, arena);
    const Tier0Code vector = tier0_compile(function, arena);
    *error = scalar.error;
    bool ready = tier0_test_expect(tally, check.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "int128", "%s: rule \"%s\" at %u, or the paths differ (%d, %d)",
                                   name, check.rule != 0 ? check.rule : "", check.node, scalar.error, vector.error);
    ready = ready && tier0_test_expect(tally, scalar.error == TIER0_OK || (depth_allowed && scalar.error == TIER0_ERROR_DEPTH), "int128",
                                       "%s: both paths refuse it with error %d at entry %u", name, scalar.error, scalar.node);
    ready = ready && scalar.error == TIER0_OK;
    ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.reference_count == 0 &&
                                                  branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                      BRANCH_SIZING_OK,
                                       "int128", "%s: an invalid record, a reference or an invalid branch column", name);
    ready = ready && tier0_test_covered(tally, "int128", name, &scalar);
    uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
    const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets) : (BranchSizing){ 0 };
    ready = ready && tier0_test_expect(tally, offsets != 0 && sizing.error == BRANCH_SIZING_OK, "int128", "%s: the branches can't be sized", name);
    const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
    const int64_t vector_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode(scalar.records, scalar.count, image, room, 0, 0) : -1;
    const int64_t scalar_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode_scalar(scalar.records, scalar.count, scratch, room, 0, 0) : -1;
    ready = ready && tier0_test_expect(tally, vector_length > 0 && vector_length == scalar_length && memcmp(image, scratch, (size_t)vector_length) == 0, "int128",
                                       "%s: the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", name, vector_length, scalar_length);
    return ready ? (uint32_t)vector_length : 0;
}

// A program's code (`length` bytes at `image`) run natively on the inputs in `shared`, and by the interpreter on the same
// ones: every output slot it wrote must have the interpreter's bytes. Returns whether it ran and every output matched,
// and then adds to *reached what the interpreter's run reached.
static bool tier0_test_int128_check(Tier0TestTally* tally, const char* name, const Tier0TestInt128Program* program, const uint8_t* image, uint32_t length,
                                    Tier0TestInt128Shared* shared, Tier0TestInt128Reach* reached)
{
    uint8_t visited[BUSTER_TIER0_TEST_INT128_ENTRIES];
    memset(shared->out, 0xa5, sizeof(shared->out));
    const bool ran = tier0_test_expect(tally, tier0_test_int128_native(image, length, tier0_test_int128_run_body, shared), "int128", "%s: the native run failed",
                                       name);
    uint8_t expected[sizeof(shared->out)];
    memset(expected, 0xa5, sizeof(expected));
    const uint64_t parameters[3] = { (uint64_t)(uintptr_t)shared->in, (uint64_t)(uintptr_t)shared->ints, (uint64_t)(uintptr_t)expected };
    const bool interpreted = tier0_test_expect(tally, tier0_test_int128_interpret(program, parameters, visited), "int128", "%s: the interpreter can't run it", name);
    bool same = ran && interpreted;
    for (uint32_t o = 0; o < BUSTER_TIER0_TEST_INT128_OUTPUTS && ran && interpreted; o += 1)
    {
        const uint32_t class = program->outputs[o];
        const uint32_t bytes = tier0_class_bytes(class);
        same = tier0_test_expect(tally, memcmp(shared->out + 16 * o, expected + 16 * o, bytes) == 0, "int128", "%s: output %u (class %u) differs from the interpreter's",
                                 name, o, class) &&
               same;
    }
    // Only a program compiled, run and checked counts toward what the programs reach (a refused one, a depth past the
    // slots, emitted none of its code), and of it only what its run reached.
    if (same)
    {
        const Tier0TestInt128Reach program_reached = tier0_test_int128_reached(program, visited);
        tier0_test_int128_reach(reached, &program_reached);
    }
    return same;
}

// Boundaries: for each 128-bit class and each pair (a, b) of boundary values, a program writes a's six compares with b,
// a shifted right by the constant counts around the halves' edges, and a shifted right and left by variable counts.
// Each output must be the interpreter's and, apart from it, what C's own operators give: __int128's signed compares and
// arithmetic shift, unsigned __int128's unsigned ones and logical shift.
#define BUSTER_TIER0_TEST_INT128_BOUNDARIES 10

static void tier0_test_int128_boundaries(Tier0TestTally* tally, const Tier0TestInt128Abi* abi, Tier0TestInt128Shared* shared)
{
    static const uint8_t counts[6] = { 0, 1, 63, 64, 65, 127 };
    // The variable counts: two right shifts, then two left ones.
    static const uint64_t variable[4] = { 127, 64, 1, 127 };
    const Tier0TestWide minimum = (Tier0TestWide)1 << 127;
    const Tier0TestWide values[BUSTER_TIER0_TEST_INT128_BOUNDARIES] = {
        0, 1, ~(Tier0TestWide)0, minimum - 1, minimum, (Tier0TestWide)1 << 63, (Tier0TestWide)1 << 64, ~(Tier0TestWide)0 << 64, ((Tier0TestWide)1 << 64) - 1,
        ~(Tier0TestWide)0 << 63,
    };
    Tier0TestInt128Program* program = calloc(1, sizeof(Tier0TestInt128Program));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, program != 0 && image != 0 && arena.base != 0, "int128", "no memory");
    for (uint32_t index = 0; index < 2 && ready; index += 1)
    {
        const uint32_t class = index == 0 ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128;
        const bool is_signed = class == BUSTER_TYPED_CLASS_I128;
        memset(program, 0, sizeof(*program));
        for (uint32_t k = 0; k < 6; k += 1)
        {
            tier0_test_int128_element(program, 2, k, 16);
            tier0_test_int128_element(program, 0, 0, 16);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_test_int128_element(program, 0, 1, 16);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_EQ + k, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I32, 0);
            program->outputs[k] = BUSTER_TYPED_CLASS_I32;
        }
        for (uint32_t k = 0; k < 6; k += 1)
        {
            tier0_test_int128_element(program, 2, 6 + k, 16);
            tier0_test_int128_element(program, 0, 0, 16);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, counts[k]);
            tier0_test_int128_emit(program, BUSTER_TYPED_SHR, class, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_SET, class, 0);
            program->outputs[6 + k] = (uint8_t)class;
        }
        for (uint32_t k = 0; k < 4; k += 1)
        {
            tier0_test_int128_element(program, 2, 12 + k, 16);
            tier0_test_int128_element(program, 0, 0, 16);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_test_int128_element(program, 1, k, 8);
            tier0_test_int128_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 127);
            tier0_test_int128_emit(program, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_emit(program, k < 2 ? BUSTER_TYPED_SHR : BUSTER_TYPED_SHL, class, 0);
            tier0_test_int128_emit(program, BUSTER_TYPED_SET, class, 0);
            program->outputs[12 + k] = (uint8_t)class;
        }
        tier0_test_int128_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        char name[64];
        snprintf(name, sizeof(name), "boundaries of class %u", class);
        arena_pop_to(&arena, 0);
        const Tier0Function function = tier0_test_int128_function(program, abi);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_int128_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, false, &error);
        for (uint32_t pair = 0; pair < BUSTER_TIER0_TEST_INT128_BOUNDARIES * BUSTER_TIER0_TEST_INT128_BOUNDARIES && length != 0; pair += 1)
        {
            const Tier0TestWide a = values[pair / BUSTER_TIER0_TEST_INT128_BOUNDARIES];
            const Tier0TestWide b = values[pair % BUSTER_TIER0_TEST_INT128_BOUNDARIES];
            memset(shared->in, 0, sizeof(shared->in));
            memset(shared->ints, 0, sizeof(shared->ints));
            shared->in[0] = a;
            shared->in[1] = b;
            memcpy(shared->ints, variable, sizeof(variable));
            char case_name[96];
            snprintf(case_name, sizeof(case_name), "%s, values %u and %u", name, pair / BUSTER_TIER0_TEST_INT128_BOUNDARIES, pair % BUSTER_TIER0_TEST_INT128_BOUNDARIES);
            Tier0TestInt128Reach boundaries_reached = { 0 };
            const bool same = tier0_test_int128_check(tally, case_name, program, image, length, shared, &boundaries_reached);
            const bool less = is_signed ? (__int128)a < (__int128)b : a < b;
            const bool compares[6] = { a == b, a != b, less, less || a == b, !less && a != b, !less };
            for (uint32_t k = 0; k < 6 && same; k += 1)
            {
                int32_t result = 0;
                memcpy(&result, shared->out + 16 * k, sizeof(result));
                tier0_test_expect(tally, result == (int32_t)compares[k], "int128", "%s: compare %u gives %d", case_name, k, result);
            }
            for (uint32_t k = 0; k < 10 && same; k += 1)
            {
                const uint32_t n = k < 6 ? counts[k] : (uint32_t)(variable[k - 6] & 127);
                const bool left = k >= 8;
                const Tier0TestWide expected = left ? a << n : is_signed ? (Tier0TestWide)((__int128)a >> n) : a >> n;
                Tier0TestWide result = 0;
                memcpy(&result, shared->out + 16 * (6 + k), sizeof(result));
                tier0_test_expect(tally, result == expected, "int128", "%s: the shift %s by %u gives %016" PRIx64 "%016" PRIx64 ", %016" PRIx64 "%016" PRIx64 " expected",
                                  case_name, left ? "left" : "right", n, (uint64_t)(result >> 64), (uint64_t)result, (uint64_t)(expected >> 64), (uint64_t)expected);
            }
        }
    }
    arena_release(&arena);
    free(image);
    free(program);
}

// Random programs: each generated, compiled and encoded, then run natively on random inputs and by the interpreter on the
// same ones, every output slot it wrote compared byte for byte. With the default count, the programs must reach every
// kind of 128-bit entry, every conversion, and the fixed registers' saves.
static void tier0_test_int128_programs(Tier0TestTally* tally, const Tier0TestInt128Abi* abi, uint64_t seed, uint32_t count, Tier0TestInt128Shared* shared,
                                       uint32_t* too_deep, uint64_t* entries)
{
    Tier0TestInt128Reach reached = { 0 };
    Tier0TestInt128Program* program = calloc(1, sizeof(Tier0TestInt128Program));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, program != 0 && image != 0 && arena.base != 0, "int128", "no memory");
    for (uint32_t p = 0; p < count && ready; p += 1)
    {
        uint64_t state = seed ^ (0x128ull << 32) ^ p;
        tier0_test_int128_program(program, &state);
        arena_pop_to(&arena, 0);
        char name[64];
        snprintf(name, sizeof(name), "program %u", p);
        if (!tier0_test_expect(tally, !program->overflow, "int128", "%s: too large for the generator's columns", name))
        {
            continue;
        }
        const Tier0Function function = tier0_test_int128_function(program, abi);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_int128_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, true, &error);
        *too_deep += error == TIER0_ERROR_DEPTH ? 1 : 0;
        *entries += program->count;
        if (length == 0)
        {
            continue;
        }
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_IN; k += 1)
        {
            shared->in[k] = tier0_test_int128_random(&state);
        }
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_INTS; k += 1)
        {
            shared->ints[k] = tier0_test_int128_random64(&state);
        }
        tier0_test_int128_check(tally, name, program, image, length, shared, &reached);
    }
    const Tier0TestInt128Reach wanted = tier0_test_int128_wanted();
    for (uint32_t index = 0; index < 2; index += 1)
    {
        const uint64_t missing_kinds = wanted.kinds[index] & ~reached.kinds[index];
        const uint32_t missing_to = wanted.to[index] & ~reached.to[index];
        const uint32_t missing_from = wanted.from[index] & ~reached.from[index];
        const uint32_t missing_saves = wanted.saves[index] & ~reached.saves[index];
        const uint32_t missing_arms = wanted.arms[index] & ~reached.arms[index];
        const bool missing = missing_kinds != 0 || missing_to != 0 || missing_from != 0 || missing_saves != 0 || missing_arms != 0;
        tier0_test_expect(tally, count < BUSTER_TIER0_TEST_INT128_PROGRAMS || !missing, "int128",
                          "the checked programs' runs don't reach every kind, conversion, save and arm of %s (missing kinds %#" PRIx64
                          ", conversions to it from %#x and from it to %#x, saves %#x, arms %#x)",
                          index == 0 ? "I128" : "U128", missing_kinds, missing_to, missing_from, missing_saves, missing_arms);
    }
    arena_release(&arena);
    free(image);
    free(program);
}

// ---------------------------------------------------------------------------------------------------------------------
// Functions of 128-bit parameters and results, called from C

// A hand-written function: its entries, locals (16-byte objects for the 128-bit parameters, results and temporaries, or
// values) and calls, as Tier 0 takes them.
typedef struct Tier0TestInt128Written Tier0TestInt128Written;
struct Tier0TestInt128Written
{
    uint8_t kinds[64];
    uint8_t classes[64];
    uint64_t values[64];
    uint32_t count;
    uint8_t local_classes[12];
    uint8_t local_flags[12];
    uint32_t local_sizes[12];
    uint8_t local_alignments[12];
    uint32_t local_count;
    uint32_t parameter_count;
    SymbolId call_targets[2];
    uint32_t call_first[2];
    uint32_t call_counts[2];
    uint8_t call_variadic[2];
    uint32_t call_result_objects[2];
    AbiCall calls[2];
    uint8_t argument_classes[8];
    AbiLocation argument_locations[8];
    uint32_t argument_sizes[8];
    uint32_t call_count;
    uint32_t argument_count;
};

static void tier0_test_int128_put(Tier0TestInt128Written* written, uint32_t kind, uint32_t class, uint64_t value)
{
    written->kinds[written->count] = (uint8_t)kind;
    written->classes[written->count] = (uint8_t)class;
    written->values[written->count] = value;
    written->count += 1;
}

// Local `local` as a 16-byte object aligned to 16 (`class` VOID), or a value of `class`.
static void tier0_test_int128_local(Tier0TestInt128Written* written, uint32_t local, uint32_t class)
{
    const bool object = class == BUSTER_TYPED_CLASS_VOID;
    written->local_classes[local] = (uint8_t)class;
    written->local_flags[local] = object ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
    written->local_sizes[local] = object ? 16 : 0;
    written->local_alignments[local] = object ? 4 : 0;
    written->local_count = local + 1 > written->local_count ? local + 1 : written->local_count;
}

// An indirect call of signature `signature`'s C function, its arguments of `classes` (U64 for a 128-bit one, its object's
// address, with its 16 bytes in `sizes`), its result into object `object` (1 + its local) when it is a 128-bit one.
static void tier0_test_int128_written_call(Tier0TestInt128Written* written, const Tier0TestInt128Abi* abi, uint32_t signature, const uint8_t* classes,
                                           const uint32_t* sizes, uint32_t count, uint32_t object)
{
    const uint32_t call = written->call_count;
    written->call_targets[call] = 0;
    written->call_first[call] = written->argument_count;
    written->call_counts[call] = count;
    written->call_variadic[call] = 0;
    written->call_result_objects[call] = object;
    written->calls[call] = abi->calls[signature];
    for (uint32_t a = 0; a < count; a += 1)
    {
        written->argument_classes[written->argument_count] = classes[a];
        written->argument_sizes[written->argument_count] = sizes[a];
        written->argument_locations[written->argument_count] = abi->locations[abi->first[signature] + a];
        written->argument_count += 1;
    }
    written->call_count += 1;
}

static Tier0Function tier0_test_int128_written_function(const Tier0TestInt128Written* written, const Tier0TestInt128Abi* abi, uint32_t signature, uint32_t result_size)
{
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = written->kinds;
    function.classes = written->classes;
    function.values = written->values;
    function.count = written->count;
    function.local_classes = written->local_classes;
    function.local_flags = written->local_flags;
    function.local_sizes = written->local_sizes;
    function.local_alignments = written->local_alignments;
    function.local_count = written->local_count;
    function.parameter_count = written->parameter_count;
    function.parameters = abi->locations + abi->first[signature];
    function.result = abi->calls[signature].result;
    function.result_size = result_size;
    function.call_targets = written->call_targets;
    function.call_first = written->call_first;
    function.call_argument_counts = written->call_counts;
    function.call_variadic = written->call_variadic;
    function.calls = written->calls;
    function.argument_locations = written->argument_locations;
    function.argument_classes = written->argument_classes;
    function.call_count = written->call_count;
    function.argument_count = written->argument_count;
    function.argument_sizes = written->call_count != 0 ? written->argument_sizes : 0;
    function.call_result_objects = written->call_count != 0 ? written->call_result_objects : 0;
    function.int128 = tier0_test_int128_has(&function);
    return function;
}

// The functions' arguments: boundary values of 128 bits and of 64.
static Tier0TestWide tier0_test_int128_argument(uint32_t k, uint32_t which)
{
    static const uint64_t halves[8][2] = {
        { 0, 0 },
        { 0, 1 },
        { ~0ull, ~0ull },
        { 0x8000000000000000ull, 0 },
        { 0x7fffffffffffffffull, ~0ull },
        { 0x0123456789abcdefull, 0xfedcba9876543210ull },
        { 1, 0 },
        { 0xffffffffull, 0x8000000000000001ull },
    };
    const uint32_t index = (k + 3 * which) % 8;
    return tier0_test_int128_make(halves[index][0], halves[index][1]);
}

static int64_t tier0_test_int128_long(uint32_t k, uint32_t which)
{
    static const int64_t longs[8] = { 0, 1, -1, INT64_MIN, INT64_MAX, 0x7fffffff, -0x80000000ll, 12345 };
    return longs[(k + 5 * which) % 8];
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_int128_run_muladd(const void* code, Tier0TestInt128Shared* shared)
{
    typedef __int128 (*F)(__int128, unsigned __int128, int64_t);
    F f = 0;
    memcpy(&f, &code, sizeof(f));
    for (uint32_t k = 0; k < 16; k += 1)
    {
        shared->results[k] = (Tier0TestWide)f((__int128)tier0_test_int128_argument(k, 0), tier0_test_int128_argument(k, 1), tier0_test_int128_long(k, 0));
    }
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_int128_run_stacked(const void* code, Tier0TestInt128Shared* shared)
{
    typedef unsigned __int128 (*F)(int64_t, int64_t, int64_t, int64_t, int64_t, unsigned __int128);
    F f = 0;
    memcpy(&f, &code, sizeof(f));
    for (uint32_t k = 0; k < 16; k += 1)
    {
        shared->results[k] = f(tier0_test_int128_long(k, 0), tier0_test_int128_long(k, 1), tier0_test_int128_long(k, 2), tier0_test_int128_long(k, 3),
                               tier0_test_int128_long(k, 4), tier0_test_int128_argument(k, 2));
    }
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_int128_run_caller(const void* code, Tier0TestInt128Shared* shared)
{
    typedef __int128 (*F)(__int128);
    F f = 0;
    memcpy(&f, &code, sizeof(f));
    for (uint32_t k = 0; k < 16; k += 1)
    {
        shared->results[k] = (Tier0TestWide)f((__int128)tier0_test_int128_argument(k, 3));
    }
}

// What C computes of function `which`'s arguments `k`.
static Tier0TestWide tier0_test_int128_expected(uint32_t which, uint32_t k)
{
    Tier0TestWide expected = 0;
    if (which == 0)
    {
        expected = tier0_test_int128_argument(k, 0) * tier0_test_int128_argument(k, 1) + (Tier0TestWide)(__int128)tier0_test_int128_long(k, 0);
    }
    else if (which == 1)
    {
        const Tier0TestWide x = tier0_test_int128_argument(k, 2);
        expected = (x >> 3) + (Tier0TestWide)(__int128)tier0_test_int128_long(k, 4) - (Tier0TestWide)(__int128)tier0_test_int128_long(k, 0);
    }
    else
    {
        const __int128 a = (__int128)tier0_test_int128_argument(k, 3);
        const Tier0TestWide twice = (Tier0TestWide)tier0_test_int128_twice(a, 9);
        expected = twice + tier0_test_int128_stacked(1, 2, 3, 4, 5, (Tier0TestWide)a) + 1;
    }
    return expected;
}

// The functions, each compiled and run from C on 16 sets of arguments, and each result what C computes:
//   - muladd(a, b, c) = a * b + c: two 128-bit parameters in register pairs (objects the prologue fills), a long in r8,
//     the result in rax and rdx (an object's bytes, which the epilogue loads);
//   - stacked(a, b, c, d, e, x) = (x >> 3) + e - a: x on the stack, its home its incoming bytes;
//   - caller(a) = twice(a, 9) + stacked(1, 2, 3, 4, 5, a) + 1: 128-bit arguments (objects' addresses) in a register pair
//     and on the stack, and results in rax and rdx into objects, the first sum kept in its slot across the second call.
static void tier0_test_int128_functions(Tier0TestTally* tally, const Tier0TestInt128Abi* abi, Tier0TestInt128Shared* shared)
{
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, image != 0 && arena.base != 0, "int128", "no memory");
    for (uint32_t which = 0; which < 3 && ready; which += 1)
    {
        static const char* const names[3] = { "muladd", "stacked", "caller" };
        static const uint32_t signatures[3] = { BUSTER_TIER0_TEST_INT128_MULADD, BUSTER_TIER0_TEST_INT128_STACKED, BUSTER_TIER0_TEST_INT128_CALLER };
        static const Tier0TestInt128Run runs[3] = { tier0_test_int128_run_muladd, tier0_test_int128_run_stacked, tier0_test_int128_run_caller };
        const uint32_t s = BUSTER_TYPED_CLASS_I128;
        const uint32_t u = BUSTER_TYPED_CLASS_U128;
        Tier0TestInt128Written written;
        memset(&written, 0, sizeof(written));
        if (which == 0)
        {
            // Locals: a, b (objects), c (I64), the result's object.
            tier0_test_int128_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            tier0_test_int128_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            tier0_test_int128_local(&written, 2, BUSTER_TYPED_CLASS_I64);
            tier0_test_int128_local(&written, 3, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 3;
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_MUL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_ADD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (which == 1)
        {
            // Locals: a to e (I64), x (an object, on the stack), the result's object.
            for (uint32_t l = 0; l < 5; l += 1)
            {
                tier0_test_int128_local(&written, l, BUSTER_TYPED_CLASS_I64);
            }
            tier0_test_int128_local(&written, 5, BUSTER_TYPED_CLASS_VOID);
            tier0_test_int128_local(&written, 6, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 6;
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 6);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 5);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_SHR, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 4);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_ADD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SUB, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 6);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else
        {
            // Locals: a (an object), twice's argument and result objects, stacked's argument and result objects, the
            // result's object.
            static const uint8_t twice_classes[2] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64 };
            static const uint32_t twice_sizes[2] = { 16, 0 };
            static const uint8_t stacked_classes[6] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64,
                                                        BUSTER_TYPED_CLASS_U64 };
            static const uint32_t stacked_sizes[6] = { 0, 0, 0, 0, 0, 16 };
            for (uint32_t l = 0; l < 6; l += 1)
            {
                tier0_test_int128_local(&written, l, BUSTER_TYPED_CLASS_VOID);
            }
            written.parameter_count = 1;
            tier0_test_int128_written_call(&written, abi, BUSTER_TIER0_TEST_INT128_TWICE, twice_classes, twice_sizes, 2, 3);
            tier0_test_int128_written_call(&written, abi, BUSTER_TIER0_TEST_INT128_STACKED, stacked_classes, stacked_sizes, 6, 5);
            // The result's address first, under everything.
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 5);
            // twice(a, 9): a copied into its argument's object, whose address passes.
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_int128_twice);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 9);
            tier0_test_int128_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, u, 0);
            // stacked(1, 2, 3, 4, 5, a), with the first sum in its slot below the call's callee.
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_int128_stacked);
            for (uint64_t n = 1; n <= 5; n += 1)
            {
                tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, n);
            }
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 4);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOAD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_ADD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, u, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_ADD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 5);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        arena_pop_to(&arena, 0);
        const Tier0Function function = tier0_test_int128_written_function(&written, abi, signatures[which], 16);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_int128_encode(tally, names[which], &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, false, &error);
        const bool ran = length != 0 && tier0_test_expect(tally, tier0_test_int128_native(image, length, runs[which], shared), "int128", "%s: the native run failed", names[which]);
        for (uint32_t k = 0; k < 16 && ran; k += 1)
        {
            const Tier0TestWide expected = tier0_test_int128_expected(which, k);
            tier0_test_expect(tally, shared->results[k] == expected, "int128", "%s: arguments %u: %016" PRIx64 "%016" PRIx64 ", %016" PRIx64 "%016" PRIx64 " expected",
                              names[which], k, (uint64_t)(shared->results[k] >> 64), (uint64_t)shared->results[k], (uint64_t)(expected >> 64), (uint64_t)expected);
        }
    }
    arena_release(&arena);
    free(image);
}

// ---------------------------------------------------------------------------------------------------------------------
// The memory classes' slots

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void tier0_test_int128_run_slots(const void* code, Tier0TestInt128Shared* shared)
{
    typedef void (*F)(void*);
    F f = 0;
    memcpy(&f, &code, sizeof(f));
    f(shared->out);
}

// A stack of `values` constants of `class` (I128, or F80 with its constant 1.0) above the output's address, summed and
// stored: their slots run to depth `values`, past the integer table from 15 (BUSTER_TIER0_INTEGER_DEPTHS) on, where the
// values below take the deep slots and theirs stay in the memory classes' slots at their true depths (#780). Both paths
// compile it to the same code, which stores the sum, at the table's last slot, one and two past it and 40; and refuse it
// (TIER0_ERROR_DEPTH) past BUSTER_TIER0_MAX_DEPTH alone: 239 constants and the address make a stack of 240 at the first
// addition, where 238 compile.
static void tier0_test_int128_slots(Tier0TestTally* tally, const Tier0TestInt128Abi* abi, Tier0TestInt128Shared* shared)
{
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, image != 0 && arena.base != 0, "int128", "no memory");
    static const uint8_t local_classes[1] = { BUSTER_TYPED_CLASS_U64 };
    static const uint8_t local_flags[1] = { 0 };
    const uint64_t one[2] = { 1ull << 63, 0x3fff };
    static const uint32_t stacks[] = { BUSTER_TIER0_INTEGER_DEPTHS - 1, BUSTER_TIER0_INTEGER_DEPTHS, BUSTER_TIER0_INTEGER_DEPTHS + 1, 40, BUSTER_TIER0_MAX_DEPTH - 2,
                                       BUSTER_TIER0_MAX_DEPTH - 1 };
    for (uint32_t c = 0; c < 2 && ready; c += 1)
    {
        const uint32_t class = c == 0 ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_F80;
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(stacks); s += 1)
        {
            const uint32_t values = stacks[s];
            uint8_t kinds[2 * BUSTER_TIER0_MAX_DEPTH + 4];
            uint8_t classes[2 * BUSTER_TIER0_MAX_DEPTH + 4];
            uint64_t constants[2 * BUSTER_TIER0_MAX_DEPTH + 4];
            uint32_t count = 0;
            kinds[count] = BUSTER_TYPED_LOCAL;
            classes[count] = BUSTER_TYPED_CLASS_U64;
            constants[count++] = 0;
            for (uint32_t v = 0; v < values; v += 1)
            {
                kinds[count] = BUSTER_TYPED_CONST;
                classes[count] = (uint8_t)class;
                constants[count++] = c == 0 ? (uint64_t)v * 0x100000001ull : 0;
            }
            for (uint32_t v = 1; v < values; v += 1)
            {
                kinds[count] = BUSTER_TYPED_ADD;
                classes[count] = (uint8_t)class;
                constants[count++] = 0;
            }
            kinds[count] = BUSTER_TYPED_SET;
            classes[count] = (uint8_t)class;
            constants[count++] = 0;
            kinds[count] = BUSTER_TYPED_RETURN;
            classes[count] = BUSTER_TYPED_CLASS_VOID;
            constants[count++] = 0;
            Tier0Function function;
            memset(&function, 0, sizeof(function));
            function.kinds = kinds;
            function.classes = classes;
            function.values = constants;
            function.count = count;
            function.local_classes = local_classes;
            function.local_flags = local_flags;
            function.local_count = 1;
            function.parameter_count = 1;
            function.parameters = abi->locations + abi->first[BUSTER_TIER0_TEST_INT128_BODY];
            function.result = abi->calls[BUSTER_TIER0_TEST_INT128_BODY].result;
            function.x87_constants = one;
            function.x87_constant_count = 1;
            function.x87 = class == BUSTER_TYPED_CLASS_F80;
            function.int128 = class == BUSTER_TYPED_CLASS_I128;
            char name[64];
            snprintf(name, sizeof(name), "%u constants of class %u", values, class);
            arena_pop_to(&arena, 0);
            const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
            const Tier0Code vector = tier0_compile(&function, &arena);
            const bool fits = values + 1 < BUSTER_TIER0_MAX_DEPTH;
            tier0_test_expect(tally, tier0_test_same_code(&scalar, &vector) && (fits ? scalar.error == TIER0_OK : scalar.error == TIER0_ERROR_DEPTH), "int128",
                              "%s: the paths give %d and %d", name, scalar.error, vector.error);
            tier0_test_expect(tally, !fits || values <= BUSTER_TIER0_INTEGER_DEPTHS - 1 || scalar.deep != 0, "int128", "%s: no deep entries", name);
            // The deepest stacks' code is past the image's pages: they are compiled alone.
            const bool run = values <= 40;
            Tier0Error error = TIER0_OK;
            const uint32_t length = run ? tier0_test_int128_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, false, &error) : 0;
            memset(shared->out, 0xa5, sizeof(shared->out));
            const bool ran = length != 0 && tier0_test_expect(tally, tier0_test_int128_native(image, length, tier0_test_int128_run_slots, shared), "int128",
                                                              "%s: the native run failed", name);
            if (ran && class == BUSTER_TYPED_CLASS_I128)
            {
                Tier0TestWide sum = 0;
                for (uint32_t v = 0; v < values; v += 1)
                {
                    sum += (Tier0TestWide)(__int128)(int64_t)((uint64_t)v * 0x100000001ull);
                }
                tier0_test_expect(tally, memcmp(shared->out, &sum, 16) == 0, "int128", "%s: the sum differs", name);
            }
            else if (ran)
            {
                long double sum = 0;
                memcpy(&sum, shared->out, 10);
                tier0_test_expect(tally, sum == (long double)values, "int128", "%s: the sum is %Lg", name, sum);
            }
        }
    }
    arena_release(&arena);
    free(image);
}

// ---------------------------------------------------------------------------------------------------------------------
// The rules

// A break of the classes' rules, refused at entry `node`, or (`node` UINT32_MAX) a stream it takes.
typedef struct Tier0TestInt128Break Tier0TestInt128Break;
struct Tier0TestInt128Break
{
    const char* name;
    uint32_t node;
};

// Each break of the stream's rules for the 128-bit integers, at its entry, then the valid ones beside them: a value local
// of the class, a global read or stored as one, division, a conversion to and from a floating class, a branch on one, a
// return of one, a call's argument and result of one, a shift by one, an entry of the class in a function without
// int128, and LOCAL or SET_LOCAL of one on a value local, on an object smaller than 16 bytes and on a va_list.
static void tier0_test_int128_rules(Tier0TestTally* tally, const Tier0TestInt128Abi* abi)
{
    static const Tier0TestInt128Break breaks[] = {
        { "a value local of the class", 0 },
        { "GLOBAL", 0 },
        { "SET_GLOBAL", 1 },
        { "DIV", 2 },
        { "MOD", 2 },
        { "a conversion to F64", 1 },
        { "a conversion from F64", 1 },
        { "a branch on it", 1 },
        { "a return of it", 1 },
        { "a call's argument of it", 0 },
        { "a call's result of it", 2 },
        { "a shift by it", 2 },
        { "an entry of the class without int128", 0 },
        { "LOCAL of a value local", 0 },
        { "LOCAL of an object of 8 bytes", 0 },
        { "SET_LOCAL into a va_list", 1 },
        { "valid: LOCAL, STORE_LOCAL and SET_LOCAL of objects of 16 and 24 bytes", UINT32_MAX },
        { "valid: NOT, AND, a shift by a U8, conversions and a compare", UINT32_MAX },
    };
    const uint32_t s = BUSTER_TYPED_CLASS_I128;
    const uint32_t u = BUSTER_TYPED_CLASS_U128;
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(breaks); b += 1)
    {
        Tier0TestInt128Written written;
        memset(&written, 0, sizeof(written));
        uint32_t signature = BUSTER_TIER0_TEST_INT128_BODY;
        switch (b)
        {
        case 0:
            tier0_test_int128_local(&written, 0, s);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 1:
            tier0_test_int128_put(&written, BUSTER_TYPED_GLOBAL, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 2:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET_GLOBAL, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 3:
        case 4:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, u, 7);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, u, 3);
            tier0_test_int128_put(&written, b == 3 ? BUSTER_TYPED_DIV : BUSTER_TYPED_MOD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 5:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 7);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 6:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 7:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_BRANCH_FALSE, s, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 8:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, s, 0);
            break;
        case 9:
        {
            static const uint8_t classes[2] = { BUSTER_TYPED_CLASS_I128, BUSTER_TYPED_CLASS_I64 };
            static const uint32_t sizes[2] = { 0, 0 };
            tier0_test_int128_written_call(&written, abi, BUSTER_TIER0_TEST_INT128_TWICE, classes, sizes, 2, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_int128_twice);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        }
        case 10:
        {
            static const uint8_t classes[2] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64 };
            static const uint32_t sizes[2] = { 16, 0 };
            tier0_test_int128_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            tier0_test_int128_written_call(&written, abi, BUSTER_TIER0_TEST_INT128_TWICE, classes, sizes, 2, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_int128_twice);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CALL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        }
        case 11:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, u, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_SHL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 12:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 13:
        case 14:
            // A U64's local, and an object with room for half the value.
            tier0_test_int128_local(&written, 0, b == 13 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID);
            written.local_sizes[0] = b == 13 ? 0 : 8;
            written.local_alignments[0] = b == 13 ? 0 : 3;
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 15:
            // A va_list's descriptor, which only LOCAL_ADDRESS reaches.
            tier0_test_int128_local(&written, 0, BUSTER_TYPED_CLASS_U64);
            written.local_flags[0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST;
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET_LOCAL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 16:
            tier0_test_int128_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            tier0_test_int128_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            written.local_sizes[1] = 24;
            written.local_alignments[1] = 3;
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 5);
            tier0_test_int128_put(&written, BUSTER_TYPED_SET_LOCAL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_STORE_LOCAL, u, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_LOCAL, u, 1);
            tier0_test_int128_put(&written, BUSTER_TYPED_ADD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        default:
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 5);
            tier0_test_int128_put(&written, BUSTER_TYPED_NOT, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, s, 3);
            tier0_test_int128_put(&written, BUSTER_TYPED_AND, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U8, 9);
            tier0_test_int128_put(&written, BUSTER_TYPED_SHL, s, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONVERT, u, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_CONST, u, 2);
            tier0_test_int128_put(&written, BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_int128_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        }
        Tier0Function function = tier0_test_int128_written_function(&written, abi, signature, 0);
        function.parameter_count = written.parameter_count;
        function.parameters = written.parameter_count != 0 ? abi->locations + abi->first[signature] : 0;
        function.result = (AbiReturn){ 0 };
        // A 128-bit integer entry in a function that doesn't say it has them, the last break's.
        function.int128 = tier0_test_int128_has(&function) && b != 12;
        const Tier0Check check = tier0_validate(&function);
        const bool expected = breaks[b].node == UINT32_MAX ? check.error == TIER0_OK : check.error == TIER0_ERROR_INVALID && check.node == breaks[b].node;
        tier0_test_expect(tally, expected, "int128", "%s: tier0_validate says %s at %u (%s)", breaks[b].name, check.error == TIER0_OK ? "valid" : "invalid", check.node,
                          check.rule != 0 ? check.rule : "");
    }
}

// The int128 check: the rules, the slots, the functions called from C and calling C, then `count` random programs.
static void tier0_test_int128(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, uint32_t* too_deep, uint64_t* entries)
{
    Tier0TestInt128Abi abi;
    Tier0TestInt128Shared* shared = mmap(0, sizeof(Tier0TestInt128Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const bool ready = tier0_test_expect(tally, shared != MAP_FAILED && tier0_test_int128_abi(tables, &abi), "int128", "no memory, or the signatures' types");
    if (ready)
    {
        tier0_test_int128_rules(tally, &abi);
        tier0_test_int128_slots(tally, &abi, shared);
        tier0_test_int128_functions(tally, &abi, shared);
        tier0_test_int128_boundaries(tally, &abi, shared);
        tier0_test_int128_programs(tally, &abi, seed, count, shared, too_deep, entries);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestInt128Shared));
    }
}
