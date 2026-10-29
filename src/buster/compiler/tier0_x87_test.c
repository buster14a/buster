// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's x87 check (#136's item 8): long double in Tier 0. Functions over long doubles are compiled by both paths,
// which must give the same records, encoded by both encoders, which must give the same bytes, and run natively in a child
// process, called from C through function pointers, so that this file's C (clang's code) is the oracle of the calling
// convention for long double arguments, parameters and results, and its long double arithmetic (the x87's, as Tier 0's)
// the oracle of their values:
//   - random programs: statements that each write a value into their 16 bytes of an output (a long double; an integer,
//     a float or a double converted from one; a compare's result), from inputs of every class, through constants,
//     arithmetic, ?:, DUP, STORE's kept value, conversions both ways, calls of C functions of long double arguments
//     and results, and frame objects (LOCAL, STORE_LOCAL and SET_LOCAL of the class on objects of 16 bytes and more,
//     aligned to 8 and to 16), against this file's own stack machine over the same stream (tier0_test_x87_interpret);
//   - functions of long double parameters and results, one returning a struct of a long double (in st0), one calling a C
//     function that returns such a struct, and one calling a variadic C function with long doubles, called from C;
//   - the stream's rules for the class, each break refused at its entry.
// tier0_test.c includes this file before its command, whose tally and helpers it uses.

// ---------------------------------------------------------------------------------------------------------------------
// The values

#define BUSTER_TIER0_TEST_X87_PROGRAMS 300
#define BUSTER_TIER0_TEST_X87_STATEMENTS 8
// The programs' frame objects, locals 4 to 7 after their four pointer parameters, which their last outputs copy out.
#define BUSTER_TIER0_TEST_X87_OBJECTS 4
#define BUSTER_TIER0_TEST_X87_OUTPUTS (2 * BUSTER_TIER0_TEST_X87_STATEMENTS + BUSTER_TIER0_TEST_X87_OBJECTS)
#define BUSTER_TIER0_TEST_X87_IN 8
#define BUSTER_TIER0_TEST_X87_INTS 16
#define BUSTER_TIER0_TEST_X87_FLOATS 16
#define BUSTER_TIER0_TEST_X87_CONSTANTS 24
#define BUSTER_TIER0_TEST_X87_ENTRIES 2048
#define BUSTER_TIER0_TEST_X87_CALLS 64
#define BUSTER_TIER0_TEST_X87_ARGUMENTS 192
#define BUSTER_TIER0_TEST_X87_TASKS 512
#define BUSTER_TIER0_TEST_X87_SLOTS 128
#define BUSTER_TIER0_TEST_X87_BOUNDARIES 16

// A long double from its 80 bits: the significand, then the sign and exponent.
static long double tier0_test_x87_make(uint64_t significand, uint16_t top)
{
    uint8_t bytes[16] = { 0 };
    memcpy(bytes, &significand, 8);
    memcpy(bytes + 8, &top, 2);
    long double value = 0;
    memcpy(&value, bytes, 10);
    return value;
}

// A long double's 80 bits as Tier0Function.x87_constants holds them.
static void tier0_test_x87_bits(long double value, uint64_t* significand, uint64_t* top)
{
    uint8_t bytes[16] = { 0 };
    memcpy(bytes, &value, 10);
    uint16_t high = 0;
    memcpy(significand, bytes, 8);
    memcpy(&high, bytes + 8, 2);
    *top = high;
}

// A random long double: now and then a special one (zeros, infinities, a NaN, the extremes, 0.1), mostly a normal number
// of a random significand (its integer bit set) and an exponent within 2^±64 of 1.
static long double tier0_test_x87_random(uint64_t* state, bool nan)
{
    const uint32_t shape = tier0_generate_below(state, 20);
    long double value = 0;
    if (shape == 0)
    {
        value = 0.0L;
    }
    else if (shape == 1)
    {
        value = -0.0L;
    }
    else if (shape == 2)
    {
        value = tier0_test_x87_make(1ull << 63, 0x7fff);
    }
    else if (shape == 3 && nan)
    {
        value = tier0_test_x87_make(3ull << 62, 0x7fff);
    }
    else if (shape == 4)
    {
        value = tier0_test_x87_make(~0ull, 0x7ffe);
    }
    else if (shape == 5)
    {
        value = 0.1L;
    }
    else if (shape == 6)
    {
        value = (long double)(int64_t)tier0_generate_random(state);
    }
    else
    {
        const uint64_t significand = tier0_generate_random(state) | 1ull << 63;
        const uint16_t exponent = (uint16_t)(0x3fff - 64 + tier0_generate_below(state, 128));
        const uint16_t sign = tier0_generate_chance(state, 50) ? 0x8000 : 0;
        value = tier0_test_x87_make(significand, (uint16_t)(exponent | sign));
    }
    return value;
}

// Whether two long doubles are the same 80 bits, or both NaNs (whose sign and payload the x87's operations choose, as
// clang's code does too).
static bool tier0_test_x87_same(long double a, long double b)
{
    return memcmp(&a, &b, 10) == 0 || (a != a && b != b);
}

// The C functions the programs call, as Tier 0's code calls them (through their addresses) and the interpreter does. Each
// operation is rounded alone, as the x87 rounds Tier 0's: C may contract a * b + c into one fused operation (clang's
// constant folding then rounds it once), which the pragma rules out.
static long double tier0_test_x87_mix(long double a, long double b)
{
#pragma STDC FP_CONTRACT OFF
    return a * 3 - b;
}

static long double tier0_test_x87_scale(int64_t i, long double a, double d)
{
#pragma STDC FP_CONTRACT OFF
    return (long double)i * a + (long double)d;
}

// A struct of a long double, which the convention returns in st0, and C functions that take and return them.
typedef struct Tier0TestX87Struct Tier0TestX87Struct;
struct Tier0TestX87Struct
{
    long double x;
};

static Tier0TestX87Struct tier0_test_x87_third(long double a)
{
    return (Tier0TestX87Struct){ .x = a / 3 };
}

static long double tier0_test_x87_sum(int count, ...)
{
#pragma STDC FP_CONTRACT OFF
    va_list arguments;
    va_start(arguments, count);
    long double sum = 0;
    for (int k = 0; k < count; k += 1)
    {
        sum = sum * 2 + va_arg(arguments, long double);
    }
    va_end(arguments);
    return sum;
}

// ---------------------------------------------------------------------------------------------------------------------
// The signatures

// The signatures the check's functions and calls have, and their locations from one abi_assign batch:
#define BUSTER_TIER0_TEST_X87_MIX 0
#define BUSTER_TIER0_TEST_X87_SCALE 1
#define BUSTER_TIER0_TEST_X87_BODY 2
#define BUSTER_TIER0_TEST_X87_F 3
#define BUSTER_TIER0_TEST_X87_G 4
#define BUSTER_TIER0_TEST_X87_K 5
#define BUSTER_TIER0_TEST_X87_THIRD 6
#define BUSTER_TIER0_TEST_X87_V 7
#define BUSTER_TIER0_TEST_X87_SUM 8
// The boundaries' conversions (tier0_test_x87_boundaries): a long double to an unsigned long, a long and an int, and
// back from an unsigned long and a long.
#define BUSTER_TIER0_TEST_X87_TO_U64 9
#define BUSTER_TIER0_TEST_X87_TO_I64 10
#define BUSTER_TIER0_TEST_X87_TO_I32 11
#define BUSTER_TIER0_TEST_X87_FROM_U64 12
#define BUSTER_TIER0_TEST_X87_FROM_I64 13
#define BUSTER_TIER0_TEST_X87_SIGNATURES 14

typedef struct Tier0TestX87Abi Tier0TestX87Abi;
struct Tier0TestX87Abi
{
    AbiLocation locations[32];
    uint32_t first[BUSTER_TIER0_TEST_X87_SIGNATURES];
    AbiCall calls[BUSTER_TIER0_TEST_X87_SIGNATURES];
    TypeId record;
};

// The struct's type in the tables (classified with the rest), then every signature assigned.
static bool tier0_test_x87_abi(Tier0Tables* tables, Tier0TestX87Abi* abi)
{
    const TypeMember member = {
        .name = 1,
        .type = BUSTER_TYPE_LONG_DOUBLE,
        .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
        .alignment_log2 = 0,
        .packed = false,
    };
    const TypeRecordAttributes attributes = { .packed = false, .pack_log2 = 0, .alignment_log2 = 0 };
    abi->record = type_tag(&tables->types, BUSTER_TYPE_KIND_STRUCT, 2000);
    bool valid = abi->record != 0 && type_complete_record(&tables->types, abi->record, &member, 1, attributes) && type_size(&tables->types, abi->record) == 16;
    if (valid)
    {
        abi_classify_all(&tables->abi, &tables->types);
        const TypeId ld = BUSTER_TYPE_LONG_DOUBLE;
        const TypeId results[BUSTER_TIER0_TEST_X87_SIGNATURES] = { ld, ld, BUSTER_TYPE_VOID, ld, abi->record, ld, abi->record, ld, ld,
                                                                  BUSTER_TYPE_ULONG, BUSTER_TYPE_LONG, BUSTER_TYPE_INT, ld, ld };
        uint32_t counts[BUSTER_TIER0_TEST_X87_SIGNATURES] = { 2, 3, 4, 3, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1 };
        uint32_t named[BUSTER_TIER0_TEST_X87_SIGNATURES] = { 2, 3, 4, 3, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1 };
        uint8_t variadic[BUSTER_TIER0_TEST_X87_SIGNATURES] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
        const TypeId arguments[] = {
            ld, ld,
            BUSTER_TYPE_LONG, ld, BUSTER_TYPE_DOUBLE,
            BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG,
            ld, BUSTER_TYPE_LONG, ld,
            ld,
            ld,
            ld,
            ld, ld,
            BUSTER_TYPE_INT, ld, ld,
            ld,
            ld,
            ld,
            BUSTER_TYPE_ULONG,
            BUSTER_TYPE_LONG,
        };
        uint32_t at = 0;
        for (uint32_t s = 0; s < BUSTER_TIER0_TEST_X87_SIGNATURES; s += 1)
        {
            abi->first[s] = at;
            at += counts[s];
        }
        const AbiSignatures batch = {
            .results = results,
            .counts = counts,
            .named = named,
            .variadic = variadic,
            .count = BUSTER_TIER0_TEST_X87_SIGNATURES,
            .arguments = arguments,
        };
        abi_assign(&tables->abi, batch, abi->locations, abi->calls);
        valid = at == BUSTER_ARRAY_LENGTH(arguments);
    }
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// A program's stream, constants and calls (each an indirect call of one of the C functions, its address a U64 constant
// under its arguments), and what each statement wrote (its class, or VOID for a slot it left).
typedef struct Tier0TestX87Program Tier0TestX87Program;
struct Tier0TestX87Program
{
    uint8_t kinds[BUSTER_TIER0_TEST_X87_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_X87_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_X87_ENTRIES];
    uint32_t count;
    uint64_t constants[2 * BUSTER_TIER0_TEST_X87_CONSTANTS];
    uint32_t constant_count;
    // The constants at least 1, for the conversions to an integer (below).
    uint32_t divisors[BUSTER_TIER0_TEST_X87_CONSTANTS];
    uint32_t divisor_count;
    SymbolId call_targets[BUSTER_TIER0_TEST_X87_CALLS];
    uint32_t call_first[BUSTER_TIER0_TEST_X87_CALLS];
    uint32_t call_counts[BUSTER_TIER0_TEST_X87_CALLS];
    uint8_t call_variadic[BUSTER_TIER0_TEST_X87_CALLS];
    AbiCall calls[BUSTER_TIER0_TEST_X87_CALLS];
    uint32_t call_count;
    uint8_t argument_classes[BUSTER_TIER0_TEST_X87_ARGUMENTS];
    AbiLocation argument_locations[BUSTER_TIER0_TEST_X87_ARGUMENTS];
    uint32_t argument_count;
    uint8_t outputs[BUSTER_TIER0_TEST_X87_OUTPUTS];
    // Each ?:'s branch and jump, whose targets its labels patch.
    uint32_t patches[BUSTER_TIER0_TEST_X87_SLOTS];
    uint32_t patch_count;
    bool overflow;
};

static void tier0_test_x87_emit(Tier0TestX87Program* program, uint32_t kind, uint32_t class, uint64_t value)
{
    if (program->count < BUSTER_TIER0_TEST_X87_ENTRIES)
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
static void tier0_test_x87_element(Tier0TestX87Program* program, uint32_t parameter, uint32_t index, uint32_t size)
{
    tier0_test_x87_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, parameter);
    tier0_test_x87_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, index);
    tier0_test_x87_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, size);
}

// A call of C function `signature` (MIX or SCALE), its address pushed by the caller under its arguments: its row of the
// calls table, with the signature's locations.
static uint32_t tier0_test_x87_call(Tier0TestX87Program* program, const Tier0TestX87Abi* abi, uint32_t signature)
{
    const uint32_t call = program->call_count;
    const uint32_t count = signature == BUSTER_TIER0_TEST_X87_MIX ? 2 : 3;
    static const uint8_t mix[2] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
    static const uint8_t scale[3] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F64 };
    if (call < BUSTER_TIER0_TEST_X87_CALLS && program->argument_count + count <= BUSTER_TIER0_TEST_X87_ARGUMENTS)
    {
        program->call_targets[call] = 0;
        program->call_first[call] = program->argument_count;
        program->call_counts[call] = count;
        program->call_variadic[call] = 0;
        program->calls[call] = abi->calls[signature];
        for (uint32_t a = 0; a < count; a += 1)
        {
            program->argument_classes[program->argument_count] = signature == BUSTER_TIER0_TEST_X87_MIX ? mix[a] : scale[a];
            program->argument_locations[program->argument_count] = abi->locations[abi->first[signature] + a];
            program->argument_count += 1;
        }
        program->call_count += 1;
    }
    else
    {
        program->overflow = true;
    }
    return call;
}

// The generator's tasks, popped from a stack: an expression to make (a long double's, of a budget of nodes), an entry to
// emit once its operands are, a leaf of a call's other arguments (an I64 from the integers, a double from the floats),
// and ?:'s branch, jump and labels (`value` its patch).
#define BUSTER_TIER0_TEST_X87_TASK_EXPRESSION 0
#define BUSTER_TIER0_TEST_X87_TASK_ENTRY 1
#define BUSTER_TIER0_TEST_X87_TASK_INTEGER 2
#define BUSTER_TIER0_TEST_X87_TASK_DOUBLE 3
#define BUSTER_TIER0_TEST_X87_TASK_BRANCH 4
#define BUSTER_TIER0_TEST_X87_TASK_JUMP 5
#define BUSTER_TIER0_TEST_X87_TASK_LABEL 6

typedef struct Tier0TestX87Task Tier0TestX87Task;
struct Tier0TestX87Task
{
    uint8_t task;
    uint8_t kind;
    uint8_t class;
    uint32_t budget;
    uint64_t value;
};

typedef struct Tier0TestX87Tasks Tier0TestX87Tasks;
struct Tier0TestX87Tasks
{
    Tier0TestX87Task items[BUSTER_TIER0_TEST_X87_TASKS];
    uint32_t count;
};

static void tier0_test_x87_push(Tier0TestX87Program* program, Tier0TestX87Tasks* tasks, Tier0TestX87Task task)
{
    if (tasks->count < BUSTER_TIER0_TEST_X87_TASKS)
    {
        tasks->items[tasks->count] = task;
        tasks->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

static Tier0TestX87Task tier0_test_x87_entry(uint32_t kind, uint32_t class, uint64_t value)
{
    return (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_ENTRY, .kind = (uint8_t)kind, .class = (uint8_t)class, .value = value };
}

static Tier0TestX87Task tier0_test_x87_expression(uint32_t budget)
{
    return (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_EXPRESSION, .class = BUSTER_TYPED_CLASS_F80, .budget = budget };
}

// A leaf of a long double: an input, a constant, an integer of any class or a float or a double converted, or a frame
// object's value.
static void tier0_test_x87_leaf(Tier0TestX87Program* program, uint64_t* state)
{
    static const uint8_t integers[8] = {
        BUSTER_TYPED_CLASS_I8, BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_I16, BUSTER_TYPED_CLASS_U16,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64,
    };
    const uint32_t choice = tier0_generate_below(state, 10);
    if (choice < 3)
    {
        tier0_test_x87_element(program, 0, tier0_generate_below(state, BUSTER_TIER0_TEST_X87_IN), 16);
        tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
    }
    else if (choice < 5)
    {
        tier0_test_x87_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, tier0_generate_below(state, program->constant_count));
    }
    else if (choice < 7)
    {
        tier0_test_x87_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_X87_INTS), 8);
        tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, integers[tier0_generate_below(state, 8)], 0);
        tier0_test_x87_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
    }
    else if (choice < 8)
    {
        // Floats at the elements from 8 (in each element's low 4 bytes), doubles below.
        const uint32_t index = tier0_generate_below(state, BUSTER_TIER0_TEST_X87_FLOATS);
        tier0_test_x87_element(program, 2, index, 8);
        tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, index >= 8 ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64, 0);
        tier0_test_x87_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
    }
    else
    {
        tier0_test_x87_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, 4 + tier0_generate_below(state, BUSTER_TIER0_TEST_X87_OBJECTS));
    }
}

// Runs the task stack until it is empty: each expression becomes a leaf (its budget spent, or by chance) or an operation
// whose operands are new tasks, pushed after it in reverse, so that they come out in postorder.
static void tier0_test_x87_run_tasks(Tier0TestX87Program* program, const Tier0TestX87Abi* abi, Tier0TestX87Tasks* tasks, uint64_t* state)
{
    while (tasks->count != 0 && !program->overflow)
    {
        tasks->count -= 1;
        const Tier0TestX87Task task = tasks->items[tasks->count];
        if (task.task == BUSTER_TIER0_TEST_X87_TASK_ENTRY)
        {
            tier0_test_x87_emit(program, task.kind, task.class, task.value);
        }
        else if (task.task == BUSTER_TIER0_TEST_X87_TASK_INTEGER)
        {
            tier0_test_x87_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_X87_INTS), 8);
            tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
        }
        else if (task.task == BUSTER_TIER0_TEST_X87_TASK_DOUBLE)
        {
            tier0_test_x87_element(program, 2, tier0_generate_below(state, 8), 8);
            tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F64, 0);
        }
        else if (task.task == BUSTER_TIER0_TEST_X87_TASK_BRANCH || task.task == BUSTER_TIER0_TEST_X87_TASK_JUMP)
        {
            program->patches[task.value] = program->count;
            tier0_test_x87_emit(program, task.task == BUSTER_TIER0_TEST_X87_TASK_BRANCH ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_JUMP_VALUE,
                                task.task == BUSTER_TIER0_TEST_X87_TASK_BRANCH ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_F80, 0);
        }
        else if (task.task == BUSTER_TIER0_TEST_X87_TASK_LABEL)
        {
            program->values[program->patches[task.value]] = program->count;
        }
        else if (task.budget <= 1 || tier0_generate_chance(state, 25))
        {
            tier0_test_x87_leaf(program, state);
        }
        else
        {
            const uint32_t choice = tier0_generate_below(state, 100);
            const uint32_t budget = task.budget - 1;
            const uint32_t left = 1 + tier0_generate_below(state, budget > 1 ? budget - 1 : 1);
            const uint32_t right = budget > left ? budget - left : 1;
            if (choice < 40)
            {
                static const uint8_t operations[4] = { BUSTER_TYPED_ADD, BUSTER_TYPED_SUB, BUSTER_TYPED_MUL, BUSTER_TYPED_DIV };
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(operations[tier0_generate_below(state, 4)], BUSTER_TYPED_CLASS_F80, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(right));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(left));
            }
            else if (choice < 48)
            {
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_F80, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(budget));
            }
            else if (choice < 56)
            {
                // x * x, by DUP.
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_F80, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_F80, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(budget));
            }
            else if (choice < 62)
            {
                // Through a double and back, or a float and back.
                const uint32_t through = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CONVERT, through, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(budget));
            }
            else if (choice < 68)
            {
                // The value stored into a frame object, and kept (STORE_LOCAL).
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_F80, 4 + tier0_generate_below(state, BUSTER_TIER0_TEST_X87_OBJECTS)));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(budget));
            }
            else if (choice < 80 && program->patch_count + 2 <= BUSTER_TIER0_TEST_X87_SLOTS && budget >= 4)
            {
                // (a OP b) ? x : y, the arms' value carried by JUMP_VALUE.
                const uint32_t branch = program->patch_count;
                const uint32_t jump = program->patch_count + 1;
                program->patch_count += 2;
                const uint32_t compare = BUSTER_TYPED_EQ + tier0_generate_below(state, 6);
                const uint32_t quarter = budget / 4 != 0 ? budget / 4 : 1;
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_LABEL, .value = jump });
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(quarter));
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_LABEL, .value = branch });
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_JUMP, .value = jump });
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(quarter));
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_BRANCH, .value = branch });
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(compare, BUSTER_TYPED_CLASS_I32, 0));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(quarter));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(quarter));
            }
            else if (choice < 90)
            {
                // mix(a, b), through its address.
                const uint32_t call = tier0_test_x87_call(program, abi, BUSTER_TIER0_TEST_X87_MIX);
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, call));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(right));
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(left));
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_mix));
            }
            else
            {
                // scale(i, a, d): a general register, the stack and an XMM register.
                const uint32_t call = tier0_test_x87_call(program, abi, BUSTER_TIER0_TEST_X87_SCALE);
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, call));
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_DOUBLE });
                tier0_test_x87_push(program, tasks, tier0_test_x87_expression(budget));
                tier0_test_x87_push(program, tasks, (Tier0TestX87Task){ .task = BUSTER_TIER0_TEST_X87_TASK_INTEGER });
                tier0_test_x87_push(program, tasks, tier0_test_x87_entry(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_scale));
            }
        }
    }
}

// A program: its constants (some at least 1, which divide an integer converted to a long double before it converts back,
// so that the value stays in the integer's range), its frame objects given values (a constant's or an input's, by
// SET_LOCAL), then each statement into its output slots (2s and 2s + 1, 16 bytes each) or a frame object, then each
// object copied out into its own output slot (after the statements'), then RETURN.
static void tier0_test_x87_program(Tier0TestX87Program* program, const Tier0TestX87Abi* abi, uint64_t* state)
{
    memset(program, 0, sizeof(*program));
    static const long double divisors[5] = { 1.0L, 2.0L, 3.0L, 10.0L, 1.5L };
    program->constant_count = 8 + tier0_generate_below(state, BUSTER_TIER0_TEST_X87_CONSTANTS - 8);
    for (uint32_t c = 0; c < program->constant_count; c += 1)
    {
        const long double value = c < 5 ? divisors[c] : tier0_test_x87_random(state, true);
        tier0_test_x87_bits(value, &program->constants[2 * c], &program->constants[2 * c + 1]);
        program->divisors[program->divisor_count] = c;
        program->divisor_count += c < 5 ? 1 : 0;
    }
    const uint32_t statements = 1 + tier0_generate_below(state, BUSTER_TIER0_TEST_X87_STATEMENTS);
    Tier0TestX87Tasks tasks;
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_OBJECTS; k += 1)
    {
        if (tier0_generate_chance(state, 50))
        {
            tier0_test_x87_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, tier0_generate_below(state, program->constant_count));
        }
        else
        {
            tier0_test_x87_element(program, 0, tier0_generate_below(state, BUSTER_TIER0_TEST_X87_IN), 16);
            tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
        }
        tier0_test_x87_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_F80, 4 + k);
    }
    for (uint32_t s = 0; s < statements && !program->overflow; s += 1)
    {
        const uint32_t root = tier0_generate_below(state, 11);
        const uint32_t budget = 2 + tier0_generate_below(state, 9);
        tasks.count = 0;
        tier0_test_x87_element(program, 3, 2 * s, 16);
        if (root < 3)
        {
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F80, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget));
            program->outputs[2 * s] = BUSTER_TYPED_CLASS_F80;
        }
        else if (root < 5)
        {
            // out[2s] = (out[2s + 1] = x): STORE's kept value.
            tier0_test_x87_element(program, 3, 2 * s + 1, 16);
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F80, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_STORE, BUSTER_TYPED_CLASS_F80, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget));
            program->outputs[2 * s] = BUSTER_TYPED_CLASS_F80;
            program->outputs[2 * s + 1] = BUSTER_TYPED_CLASS_F80;
        }
        else if (root < 7)
        {
            // An integer of a class, from its value divided by a constant of at least 1: truncated, in its range.
            static const uint8_t integers[8] = {
                BUSTER_TYPED_CLASS_I8, BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_I16, BUSTER_TYPED_CLASS_U16,
                BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64,
            };
            const uint32_t class = integers[tier0_generate_below(state, 8)];
            tier0_test_x87_element(program, 1, tier0_generate_below(state, BUSTER_TIER0_TEST_X87_INTS), 8);
            tier0_test_x87_emit(program, BUSTER_TYPED_LOAD, class, 0);
            tier0_test_x87_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, program->divisors[tier0_generate_below(state, program->divisor_count)]);
            tier0_test_x87_emit(program, BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_emit(program, BUSTER_TYPED_CONVERT, class, 0);
            tier0_test_x87_emit(program, BUSTER_TYPED_SET, class, 0);
            program->outputs[2 * s] = (uint8_t)class;
        }
        else if (root < 8)
        {
            const uint32_t class = tier0_generate_chance(state, 50) ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_SET, class, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_CONVERT, class, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget));
            program->outputs[2 * s] = (uint8_t)class;
        }
        else if (root < 10)
        {
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I32, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_EQ + tier0_generate_below(state, 6), BUSTER_TYPED_CLASS_I32, 0));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget / 2 + 1));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget / 2 + 1));
            program->outputs[2 * s] = BUSTER_TYPED_CLASS_I32;
        }
        else
        {
            // A frame object's new value (SET_LOCAL), which the statements after it and its own output slot read: the
            // output's address goes unused.
            tier0_test_x87_emit(program, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_push(program, &tasks, tier0_test_x87_entry(BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_F80, 4 + tier0_generate_below(state, BUSTER_TIER0_TEST_X87_OBJECTS)));
            tier0_test_x87_push(program, &tasks, tier0_test_x87_expression(budget));
        }
        tier0_test_x87_run_tasks(program, abi, &tasks, state);
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_OBJECTS; k += 1)
    {
        tier0_test_x87_element(program, 3, 2 * BUSTER_TIER0_TEST_X87_STATEMENTS + k, 16);
        tier0_test_x87_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, 4 + k);
        tier0_test_x87_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F80, 0);
        program->outputs[2 * BUSTER_TIER0_TEST_X87_STATEMENTS + k] = BUSTER_TYPED_CLASS_F80;
    }
    tier0_test_x87_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

// Whether a function has long double values (Tier0Function.x87), as its producer knows: an entry of class F80, or its
// result.
static bool tier0_test_x87_has(const Tier0Function* function)
{
    return memchr(function->classes, BUSTER_TYPED_CLASS_F80, function->count) != 0 || tier0_x87_returns(function);
}

// The program as Tier 0 takes it: four pointer parameters (the long double inputs, the integers, the floats and the
// output) in their registers, its frame objects (16, 16, 24 and 32 bytes, aligned to 16, 8, 16 and 8) and its calls.
static Tier0Function tier0_test_x87_function(const Tier0TestX87Program* program, const Tier0TestX87Abi* abi)
{
    static const uint8_t local_classes[4 + BUSTER_TIER0_TEST_X87_OBJECTS] = {
        BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID,
    };
    static const uint8_t local_flags[4 + BUSTER_TIER0_TEST_X87_OBJECTS] = {
        0, 0, 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    };
    static const uint32_t local_sizes[4 + BUSTER_TIER0_TEST_X87_OBJECTS] = { 0, 0, 0, 0, 16, 16, 24, 32 };
    static const uint8_t local_alignments[4 + BUSTER_TIER0_TEST_X87_OBJECTS] = { 0, 0, 0, 0, 4, 3, 4, 3 };
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
    function.local_count = 4 + BUSTER_TIER0_TEST_X87_OBJECTS;
    function.parameter_count = 4;
    function.parameters = abi->locations + abi->first[BUSTER_TIER0_TEST_X87_BODY];
    function.result = abi->calls[BUSTER_TIER0_TEST_X87_BODY].result;
    function.call_targets = program->call_targets;
    function.call_first = program->call_first;
    function.call_argument_counts = program->call_counts;
    function.call_variadic = program->call_variadic;
    function.calls = program->calls;
    function.argument_locations = program->argument_locations;
    function.argument_classes = program->argument_classes;
    function.call_count = program->call_count;
    function.argument_count = program->argument_count;
    function.x87_constants = program->constants;
    function.x87_constant_count = program->constant_count;
    function.x87 = tier0_test_x87_has(&function);
    return function;
}

// ---------------------------------------------------------------------------------------------------------------------
// The interpreter

// A value of the stack machine: a long double, or an integer's, a float's or a double's bits.
typedef struct Tier0TestX87Value Tier0TestX87Value;
struct Tier0TestX87Value
{
    long double f;
    uint64_t bits;
};

static float tier0_test_x87_float(uint64_t bits)
{
    const uint32_t low = (uint32_t)bits;
    float value = 0;
    memcpy(&value, &low, sizeof(value));
    return value;
}

static double tier0_test_x87_double(uint64_t bits)
{
    double value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// An integer class's value from its bytes' bits, sign- or zero-extended.
static uint64_t tier0_test_x87_integer(uint32_t class, uint64_t bits)
{
    uint64_t value = bits;
    switch (class)
    {
    case BUSTER_TYPED_CLASS_I8: value = (uint64_t)(int64_t)(int8_t)bits; break;
    case BUSTER_TYPED_CLASS_U8: value = (uint8_t)bits; break;
    case BUSTER_TYPED_CLASS_I16: value = (uint64_t)(int64_t)(int16_t)bits; break;
    case BUSTER_TYPED_CLASS_U16: value = (uint16_t)bits; break;
    case BUSTER_TYPED_CLASS_I32: value = (uint64_t)(int64_t)(int32_t)bits; break;
    case BUSTER_TYPED_CLASS_U32: value = (uint32_t)bits; break;
    default: break;
    }
    return value;
}

// Interpreter marks for each executed entry and the direction of each branch.
#define BUSTER_TIER0_TEST_X87_VISITED_RAN 1
#define BUSTER_TIER0_TEST_X87_VISITED_TAKEN 2
#define BUSTER_TIER0_TEST_X87_VISITED_NOT_TAKEN 4
#define BUSTER_TIER0_TEST_X87_ARM_FIRST 1
#define BUSTER_TIER0_TEST_X87_ARM_SECOND 2
#define BUSTER_TIER0_TEST_X87_ARMS (BUSTER_TIER0_TEST_X87_ARM_FIRST | BUSTER_TIER0_TEST_X87_ARM_SECOND)

// The program's stream run as a stack machine on `parameters`, marking executed entries and branch directions.
static bool tier0_test_x87_interpret(const Tier0TestX87Program* program, const uint64_t parameters[4], uint8_t* visited)
{
    long double objects[BUSTER_TIER0_TEST_X87_OBJECTS] = { 0 };
    memset(visited, 0, program->count);
    Tier0TestX87Value stack[64];
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
        uint32_t next = i + 1;
        valid = depth + 1 < BUSTER_ARRAY_LENGTH(stack);
        visited[i] |= BUSTER_TIER0_TEST_X87_VISITED_RAN;
        if (kind == BUSTER_TYPED_CONST && class == BUSTER_TYPED_CLASS_F80)
        {
            stack[depth].f = tier0_test_x87_make(program->constants[2 * value], (uint16_t)program->constants[2 * value + 1]);
            depth += 1;
        }
        else if (kind == BUSTER_TYPED_LOCAL && class == BUSTER_TYPED_CLASS_F80)
        {
            stack[depth].f = objects[value - 4];
            depth += 1;
        }
        else if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            objects[value - 4] = stack[depth - 1].f;
            depth -= kind == BUSTER_TYPED_SET_LOCAL ? 1 : 0;
        }
        else if (kind == BUSTER_TYPED_CONST || kind == BUSTER_TYPED_LOCAL)
        {
            stack[depth].bits = kind == BUSTER_TYPED_LOCAL ? parameters[value] : value;
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
            if (class == BUSTER_TYPED_CLASS_F80)
            {
                long double loaded = 0;
                memcpy(&loaded, address, 10);
                stack[depth - 1].f = loaded;
            }
            else
            {
                memcpy(&bits, address, tier0_class_bytes(class));
                stack[depth - 1].bits = tier0_class_integer(class) ? tier0_test_x87_integer(class, bits) : bits;
            }
        }
        else if (kind == BUSTER_TYPED_CONVERT && class == BUSTER_TYPED_CLASS_F80)
        {
            Tier0TestX87Value* top = &stack[depth - 1];
            top->f = operand == BUSTER_TYPED_CLASS_F80   ? top->f
                     : operand == BUSTER_TYPED_CLASS_F32 ? (long double)tier0_test_x87_float(top->bits)
                     : operand == BUSTER_TYPED_CLASS_F64 ? (long double)tier0_test_x87_double(top->bits)
                     : tier0_class_signed(operand)        ? (long double)(int64_t)top->bits
                                                          : (long double)top->bits;
        }
        else if (kind == BUSTER_TYPED_CONVERT)
        {
            // From a long double, which every conversion the programs make but these is.
            Tier0TestX87Value* top = &stack[depth - 1];
            if (class == BUSTER_TYPED_CLASS_F32)
            {
                const float narrow = (float)top->f;
                uint32_t bits = 0;
                memcpy(&bits, &narrow, sizeof(bits));
                top->bits = bits;
            }
            else if (class == BUSTER_TYPED_CLASS_F64)
            {
                const double narrow = (double)top->f;
                memcpy(&top->bits, &narrow, sizeof(top->bits));
            }
            else
            {
                top->bits = class == BUSTER_TYPED_CLASS_U64 ? (uint64_t)top->f : tier0_test_x87_integer(class, (uint64_t)(int64_t)top->f);
            }
        }
        else if (kind == BUSTER_TYPED_NEG)
        {
            stack[depth - 1].f = -stack[depth - 1].f;
        }
        else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_DIV)
        {
            const long double a = stack[depth - 2].f;
            const long double b = stack[depth - 1].f;
            stack[depth - 2].f = kind == BUSTER_TYPED_ADD ? a + b : kind == BUSTER_TYPED_SUB ? a - b : kind == BUSTER_TYPED_MUL ? a * b : a / b;
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_DUP)
        {
            stack[depth] = stack[depth - 1];
            depth += 1;
        }
        else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
        {
            const long double a = stack[depth - 2].f;
            const long double b = stack[depth - 1].f;
            const bool holds = kind == BUSTER_TYPED_EQ ? a == b : kind == BUSTER_TYPED_NE ? a != b : kind == BUSTER_TYPED_LT ? a < b
                               : kind == BUSTER_TYPED_LE ? a <= b : kind == BUSTER_TYPED_GT ? a > b : a >= b;
            stack[depth - 2].bits = holds ? 1 : 0;
            depth -= 1;
        }
        else if (kind == BUSTER_TYPED_BRANCH_FALSE)
        {
            depth -= 1;
            next = stack[depth].bits == 0 ? (uint32_t)value : next;
            visited[i] |= stack[depth].bits == 0 ? BUSTER_TIER0_TEST_X87_VISITED_TAKEN : BUSTER_TIER0_TEST_X87_VISITED_NOT_TAKEN;
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
            if (class == BUSTER_TYPED_CLASS_F80)
            {
                memcpy(address, &stack[depth - 1].f, 10);
            }
            else
            {
                memcpy(address, &stack[depth - 1].bits, tier0_class_bytes(class));
            }
            stack[depth - 2] = stack[depth - 1];
            depth -= kind == BUSTER_TYPED_STORE ? 1 : 2;
        }
        else if (kind == BUSTER_TYPED_CALL)
        {
            const uint32_t count = program->call_counts[value];
            const Tier0TestX87Value* arguments = &stack[depth - count];
            const uint64_t callee = stack[depth - count - 1].bits;
            long double result = 0;
            if (callee == (uint64_t)(uintptr_t)&tier0_test_x87_mix)
            {
                result = tier0_test_x87_mix(arguments[0].f, arguments[1].f);
            }
            else
            {
                result = tier0_test_x87_scale((int64_t)arguments[0].bits, arguments[1].f, tier0_test_x87_double(arguments[2].bits));
            }
            depth -= count;
            stack[depth - 1].f = result;
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

// A function compiled by both paths, which must make the same code, its records valid, its branches sized, and encoded by
// both encoders into `image` (the vector one's) and `scratch` (the scalar one's), which must be the same bytes. Returns
// the code's length, 0 for a failure, and in *error Tier 0's error. Only random programs may refuse excess depth.
static uint32_t tier0_test_x87_encode(Tier0TestTally* tally, const char* name, const Tier0Function* function, Arena* arena, uint8_t* image, uint8_t* scratch,
                                      bool depth_allowed, Tier0Error* error)
{
    const Tier0Check check = tier0_validate(function);
    const Tier0Code scalar = tier0_compile_scalar(function, arena);
    const Tier0Code vector = tier0_compile(function, arena);
    *error = scalar.error;
    bool ready = tier0_test_expect(tally, check.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "x87", "%s: rule \"%s\" at %u, or the paths differ (%d, %d)", name,
                                   check.rule != 0 ? check.rule : "", check.node, scalar.error, vector.error);
    const char* error_name = scalar.error == TIER0_ERROR_UNSUPPORTED ? "TIER0_ERROR_UNSUPPORTED" : scalar.error == TIER0_ERROR_DEPTH ? "TIER0_ERROR_DEPTH"
                             : scalar.error == TIER0_ERROR_MEMORY ? "TIER0_ERROR_MEMORY" : scalar.error == TIER0_ERROR_INVALID ? "TIER0_ERROR_INVALID" : "unknown";
    ready = ready && tier0_test_expect(tally, scalar.error == TIER0_OK || (depth_allowed && scalar.error == TIER0_ERROR_DEPTH), "x87",
                                       "%s: both paths refuse it with %s (%d) at entry %u", name, error_name, scalar.error, scalar.node);
    ready = ready && scalar.error == TIER0_OK;
    ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.reference_count == 0 &&
                                                  branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                      BRANCH_SIZING_OK,
                                       "x87", "%s: an invalid record, a reference or an invalid branch column", name);
    ready = ready && tier0_test_covered(tally, "x87", name, function, &scalar);
    uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
    const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets) : (BranchSizing){ 0 };
    ready = ready && tier0_test_expect(tally, offsets != 0 && sizing.error == BRANCH_SIZING_OK, "x87", "%s: the branches can't be sized", name);
    const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
    const int64_t vector_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode(scalar.records, scalar.count, image, room, 0, 0) : -1;
    const int64_t scalar_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode_scalar(scalar.records, scalar.count, scratch, room, 0, 0) : -1;
    ready = ready && tier0_test_expect(tally, vector_length > 0 && vector_length == scalar_length && memcmp(image, scratch, (size_t)vector_length) == 0, "x87",
                                       "%s: the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", name, vector_length, scalar_length);
    return ready ? (uint32_t)vector_length : 0;
}

// What the native runs share with the test: the inputs, and per run what the code wrote and returned.
typedef struct Tier0TestX87Shared Tier0TestX87Shared;
struct Tier0TestX87Shared
{
    long double in[BUSTER_TIER0_TEST_X87_IN];
    uint64_t ints[BUSTER_TIER0_TEST_X87_INTS];
    uint64_t floats[BUSTER_TIER0_TEST_X87_FLOATS];
    uint8_t out[BUSTER_TIER0_TEST_X87_OUTPUTS * 16];
    long double results[8];
    // The boundaries' cases (tier0_test_x87_boundaries): which conversion, each case's argument and result, and the x87
    // control word after each call.
    uint32_t boundary_signature;
    uint32_t boundary_count;
    long double boundary_values[BUSTER_TIER0_TEST_X87_BOUNDARIES];
    uint64_t boundary_integers[BUSTER_TIER0_TEST_X87_BOUNDARIES];
    uint16_t boundary_controls[BUSTER_TIER0_TEST_X87_BOUNDARIES];
};

// Runs `run` on `code` (`length` bytes) in a child, which must exit with status 0: the code mapped executable at a page of
// its own, `run` calling it from C.
typedef void (*Tier0TestX87Run)(const void* code, Tier0TestX87Shared* shared);
static bool tier0_test_x87_native(const uint8_t* code, uint32_t length, Tier0TestX87Run run, Tier0TestX87Shared* shared)
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

// The programs' body, called with the shared inputs and output.
static void tier0_test_x87_run_body(const void* code, Tier0TestX87Shared* shared)
{
    typedef void (*Body)(const void*, const void*, const void*, void*);
    Body body = 0;
    memcpy(&body, &code, sizeof(body));
    body(shared->in, shared->ints, shared->floats, shared->out);
}

// Whether `bytes` and `expected` hold the same value of `class`: a long double's 10 bytes (NaNs alike), a float's or a
// double's bits (NaNs alike), an integer's bytes.
static bool tier0_test_x87_same_output(uint32_t class, const uint8_t* bytes, const uint8_t* expected)
{
    bool same = true;
    if (class == BUSTER_TYPED_CLASS_F80)
    {
        long double a = 0;
        long double b = 0;
        memcpy(&a, bytes, 10);
        memcpy(&b, expected, 10);
        same = tier0_test_x87_same(a, b);
    }
    else if (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64)
    {
        uint64_t a = 0;
        uint64_t b = 0;
        memcpy(&a, bytes, tier0_class_bytes(class));
        memcpy(&b, expected, tier0_class_bytes(class));
        const bool a_nan = class == BUSTER_TYPED_CLASS_F32 ? tier0_test_x87_float(a) != tier0_test_x87_float(a) : tier0_test_x87_double(a) != tier0_test_x87_double(a);
        const bool b_nan = class == BUSTER_TYPED_CLASS_F32 ? tier0_test_x87_float(b) != tier0_test_x87_float(b) : tier0_test_x87_double(b) != tier0_test_x87_double(b);
        same = a == b || (a_nan && b_nan);
    }
    else if (class != BUSTER_TYPED_CLASS_VOID)
    {
        same = memcmp(bytes, expected, tier0_class_bytes(class)) == 0;
    }
    return same;
}

// What the programs reach, which the default count must cover whole, in bitmaps of their own so that no bit of one stands
// for another's: each kind of long double entry (CONST, DUP, NEG, LOAD, STORE, SET, ADD to DIV, each compare, CALL,
// JUMP_VALUE, and LOCAL, STORE_LOCAL and SET_LOCAL on frame objects), and each conversion to it and from it, by the
// other class.
typedef struct Tier0TestX87Reach Tier0TestX87Reach;
struct Tier0TestX87Reach
{
    // A bit per kind, below BUSTER_TYPED_KIND_LIMIT.
    uint64_t kinds;
    // A bit per class, below BUSTER_TYPED_CLASS_LIMIT: converted to F80, and from it.
    uint16_t to;
    uint16_t from;
    uint8_t arms;
};

static Tier0TestX87Reach tier0_test_x87_reached(const Tier0TestX87Program* program, const uint8_t* visited)
{
    Tier0TestX87Reach reached = { 0 };
    for (uint32_t i = 0; i < program->count; i += 1)
    {
        const uint32_t kind = program->kinds[i];
        const uint32_t class = program->classes[i];
        const uint32_t operand = i > 0 ? program->classes[i - 1] : BUSTER_TYPED_CLASS_VOID;
        const bool ran = (visited[i] & BUSTER_TIER0_TEST_X87_VISITED_RAN) != 0;
        const bool x87 = class == BUSTER_TYPED_CLASS_F80 || (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE && operand == BUSTER_TYPED_CLASS_F80);
        if (ran && kind == BUSTER_TYPED_CONVERT && class == BUSTER_TYPED_CLASS_F80)
        {
            reached.to |= (uint16_t)(1u << operand);
        }
        else if (ran && kind == BUSTER_TYPED_CONVERT && operand == BUSTER_TYPED_CLASS_F80)
        {
            reached.from |= (uint16_t)(1u << class);
        }
        else if (ran && x87)
        {
            reached.kinds |= 1ull << kind;
        }
        // The branch target follows the first arm's JUMP_VALUE, whose class identifies the conditional.
        if (ran && kind == BUSTER_TYPED_BRANCH_FALSE && program->values[i] > 0 && program->values[i] <= program->count &&
            program->kinds[program->values[i] - 1] == BUSTER_TYPED_JUMP_VALUE && program->classes[program->values[i] - 1] == BUSTER_TYPED_CLASS_F80)
        {
            reached.arms |= (visited[i] & BUSTER_TIER0_TEST_X87_VISITED_NOT_TAKEN) != 0 ? BUSTER_TIER0_TEST_X87_ARM_FIRST : 0;
            reached.arms |= (visited[i] & BUSTER_TIER0_TEST_X87_VISITED_TAKEN) != 0 ? BUSTER_TIER0_TEST_X87_ARM_SECOND : 0;
        }
    }
    return reached;
}

// Every kind and conversion the programs are meant to reach (tier0_test_x87_reached's bits).
static Tier0TestX87Reach tier0_test_x87_wanted(void)
{
    static const uint8_t kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_DUP, BUSTER_TYPED_NEG, BUSTER_TYPED_LOAD, BUSTER_TYPED_STORE, BUSTER_TYPED_SET, BUSTER_TYPED_ADD, BUSTER_TYPED_SUB,
        BUSTER_TYPED_MUL, BUSTER_TYPED_DIV, BUSTER_TYPED_EQ, BUSTER_TYPED_NE, BUSTER_TYPED_LT, BUSTER_TYPED_LE, BUSTER_TYPED_GT, BUSTER_TYPED_GE,
        BUSTER_TYPED_CALL, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_LOCAL, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_SET_LOCAL,
    };
    Tier0TestX87Reach wanted = { 0 };
    wanted.arms = BUSTER_TIER0_TEST_X87_ARMS;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
    {
        wanted.kinds |= 1ull << kinds[k];
    }
    for (uint32_t class = BUSTER_TYPED_CLASS_I8; class <= BUSTER_TYPED_CLASS_F64; class += 1)
    {
        wanted.to |= (uint16_t)(1u << class);
        wanted.from |= (uint16_t)(1u << class);
    }
    return wanted;
}

// Random programs: each generated, compiled and encoded, then run natively on random inputs and by the interpreter on
// the same ones, every output slot it wrote compared. A depth past Tier 0's tables (deep expressions of many long doubles
// at once) is counted apart. With the default count, the programs that compiled, ran and matched must reach every kind
// of long double entry and every conversion.
static void tier0_test_x87_programs(Tier0TestTally* tally, const Tier0TestX87Abi* abi, uint64_t seed, uint32_t count, Tier0TestX87Shared* shared,
                                    uint32_t* too_deep, uint64_t* entries)
{
    Tier0TestX87Reach reached = { 0 };
    Tier0TestX87Program* program = calloc(1, sizeof(Tier0TestX87Program));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, program != 0 && image != 0 && arena.base != 0, "x87", "no memory");
    for (uint32_t p = 0; p < count && ready; p += 1)
    {
        uint64_t state = seed ^ (0x87ull << 32) ^ p;
        tier0_test_x87_program(program, abi, &state);
        arena_pop_to(&arena, 0);
        char name[64];
        snprintf(name, sizeof(name), "program %u", p);
        if (!tier0_test_expect(tally, !program->overflow, "x87", "%s: too large for the generator's columns", name))
        {
            continue;
        }
        const Tier0Function function = tier0_test_x87_function(program, abi);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_x87_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, true, &error);
        *too_deep += error == TIER0_ERROR_DEPTH ? 1 : 0;
        *entries += program->count;
        if (length == 0)
        {
            continue;
        }
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_IN; k += 1)
        {
            shared->in[k] = tier0_test_x87_random(&state, true);
        }
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_INTS; k += 1)
        {
            const uint64_t random = tier0_generate_random(&state);
            shared->ints[k] = k < 4 ? random % 9 : random;
        }
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_FLOATS; k += 1)
        {
            const double d = (double)(int64_t)tier0_generate_random(&state) / 4096.0;
            const float f = (float)d;
            uint32_t f_bits = 0;
            memcpy(&f_bits, &f, sizeof(f_bits));
            shared->floats[k] = 0;
            memcpy(&shared->floats[k], k >= 8 ? (const void*)&f_bits : (const void*)&d, k >= 8 ? 4 : 8);
        }
        memset(shared->out, 0xa5, sizeof(shared->out));
        const bool ran = tier0_test_expect(tally, tier0_test_x87_native(image, length, tier0_test_x87_run_body, shared), "x87", "%s: the native run failed", name);
        uint8_t expected[sizeof(shared->out)];
        memset(expected, 0xa5, sizeof(expected));
        const uint64_t parameters[4] = { (uint64_t)(uintptr_t)shared->in, (uint64_t)(uintptr_t)shared->ints, (uint64_t)(uintptr_t)shared->floats, (uint64_t)(uintptr_t)expected };
        uint8_t visited[BUSTER_TIER0_TEST_X87_ENTRIES];
        const bool interpreted = tier0_test_expect(tally, tier0_test_x87_interpret(program, parameters, visited), "x87", "%s: the interpreter can't run it", name);
        bool same = ran && interpreted;
        for (uint32_t o = 0; o < BUSTER_TIER0_TEST_X87_OUTPUTS && ran && interpreted; o += 1)
        {
            same = tier0_test_expect(tally, tier0_test_x87_same_output(program->outputs[o], shared->out + 16 * o, expected + 16 * o), "x87",
                                     "%s: output %u (class %u) differs from the interpreter's", name, o, program->outputs[o]) &&
                   same;
        }
        // Only a program compiled, run and checked counts toward what the programs reach: a refused one (a depth past the
        // tables) emitted none of its code.
        if (same)
        {
            const Tier0TestX87Reach program_reached = tier0_test_x87_reached(program, visited);
            reached.kinds |= program_reached.kinds;
            reached.to |= program_reached.to;
            reached.from |= program_reached.from;
            reached.arms |= program_reached.arms;
        }
    }
    const Tier0TestX87Reach wanted = tier0_test_x87_wanted();
    const uint64_t missing_kinds = wanted.kinds & ~reached.kinds;
    const uint32_t missing_to = wanted.to & ~reached.to;
    const uint32_t missing_from = wanted.from & ~reached.from;
    const uint32_t missing_arms = wanted.arms & ~reached.arms;
    tier0_test_expect(tally, count < BUSTER_TIER0_TEST_X87_PROGRAMS || (missing_kinds == 0 && missing_to == 0 && missing_from == 0 && missing_arms == 0), "x87",
                      "the checked programs don't reach every kind, conversion and arm (missing kinds %#" PRIx64 ", conversions to %#x and from %#x, arms %#x)",
                      missing_kinds, missing_to, missing_from, missing_arms);
    arena_release(&arena);
    free(image);
    free(program);
}

// ---------------------------------------------------------------------------------------------------------------------
// Functions of long double parameters and results, called from C

// A hand-written function: its entries, locals (`objects` of them long doubles' or structs' objects of 16 bytes), calls
// and constants, as Tier 0 takes them.
typedef struct Tier0TestX87Written Tier0TestX87Written;
struct Tier0TestX87Written
{
    uint8_t kinds[32];
    uint8_t classes[32];
    uint64_t values[32];
    uint32_t count;
    uint8_t local_classes[4];
    uint8_t local_flags[4];
    uint32_t local_sizes[4];
    uint8_t local_alignments[4];
    uint32_t local_count;
    uint32_t parameter_count;
    SymbolId call_targets[2];
    uint32_t call_first[2];
    uint32_t call_counts[2];
    uint8_t call_variadic[2];
    uint32_t call_result_objects[2];
    AbiCall calls[2];
    uint8_t argument_classes[4];
    AbiLocation argument_locations[4];
    uint32_t call_count;
    uint32_t argument_count;
    uint64_t constants[2];
};

static void tier0_test_x87_put(Tier0TestX87Written* written, uint32_t kind, uint32_t class, uint64_t value)
{
    written->kinds[written->count] = (uint8_t)kind;
    written->classes[written->count] = (uint8_t)class;
    written->values[written->count] = value;
    written->count += 1;
}

// Local `local` as a long double's (or a struct of one's) object of 16 bytes, or a value of `class`.
static void tier0_test_x87_local(Tier0TestX87Written* written, uint32_t local, uint32_t class)
{
    const bool object = class == BUSTER_TYPED_CLASS_VOID;
    written->local_classes[local] = (uint8_t)class;
    written->local_flags[local] = object ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
    written->local_sizes[local] = object ? 16 : 0;
    written->local_alignments[local] = object ? 4 : 0;
    written->local_count = local + 1 > written->local_count ? local + 1 : written->local_count;
}

// An indirect call of signature `signature`'s C function, its arguments of `classes`.
static void tier0_test_x87_written_call(Tier0TestX87Written* written, const Tier0TestX87Abi* abi, uint32_t signature, const uint8_t* classes, uint32_t count, uint32_t object)
{
    const uint32_t call = written->call_count;
    written->call_targets[call] = 0;
    written->call_first[call] = written->argument_count;
    written->call_counts[call] = count;
    written->call_variadic[call] = signature == BUSTER_TIER0_TEST_X87_SUM ? 1 : 0;
    written->call_result_objects[call] = object;
    written->calls[call] = abi->calls[signature];
    for (uint32_t a = 0; a < count; a += 1)
    {
        written->argument_classes[written->argument_count] = classes[a];
        written->argument_locations[written->argument_count] = abi->locations[abi->first[signature] + a];
        written->argument_count += 1;
    }
    written->call_count += 1;
}

static Tier0Function tier0_test_x87_written_function(const Tier0TestX87Written* written, const Tier0TestX87Abi* abi, uint32_t signature, uint32_t result_size)
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
    function.call_result_objects = written->call_count != 0 ? written->call_result_objects : 0;
    function.x87_constants = written->constants;
    function.x87_constant_count = 1;
    function.x87 = tier0_test_x87_has(&function);
    return function;
}

// The functions' inputs, and what C computes of them.
static const long double tier0_test_x87_arguments[4][2] = {
    { 1.5L, -2.25L },
    { 0.1L, 3.0e300L },
    { -7.0e-300L, 0.0L },
    { 1.0e4000L, -1.0e-4000L },
};

static void tier0_test_x87_run_f(const void* code, Tier0TestX87Shared* shared)
{
    typedef long double (*F)(long double, int64_t, long double);
    F f = 0;
    memcpy(&f, &code, sizeof(f));
    for (uint32_t k = 0; k < 4; k += 1)
    {
        shared->results[k] = f(tier0_test_x87_arguments[k][0], (int64_t)k * 7 - 9, tier0_test_x87_arguments[k][1]);
    }
}

static void tier0_test_x87_run_g(const void* code, Tier0TestX87Shared* shared)
{
    typedef Tier0TestX87Struct (*G)(long double);
    G g = 0;
    memcpy(&g, &code, sizeof(g));
    for (uint32_t k = 0; k < 4; k += 1)
    {
        shared->results[k] = g(tier0_test_x87_arguments[k][0]).x;
    }
}

static void tier0_test_x87_run_one(const void* code, Tier0TestX87Shared* shared)
{
    typedef long double (*K)(long double);
    K k_function = 0;
    memcpy(&k_function, &code, sizeof(k_function));
    for (uint32_t k = 0; k < 4; k += 1)
    {
        shared->results[k] = k_function(tier0_test_x87_arguments[k][0]);
    }
}

static void tier0_test_x87_run_two(const void* code, Tier0TestX87Shared* shared)
{
    typedef long double (*V)(long double, long double);
    V v = 0;
    memcpy(&v, &code, sizeof(v));
    for (uint32_t k = 0; k < 4; k += 1)
    {
        shared->results[k] = v(tier0_test_x87_arguments[k][0], tier0_test_x87_arguments[k][1]);
    }
}

// What C computes of function `which`'s arguments `k`, each operation rounded alone (as tier0_test_x87_mix).
static long double tier0_test_x87_expected(uint32_t which, uint32_t k)
{
#pragma STDC FP_CONTRACT OFF
    const long double a = tier0_test_x87_arguments[k][0];
    const long double b = tier0_test_x87_arguments[k][1];
    const long double expected = which == 0 ? (a + b) * (long double)((int64_t)k * 7 - 9) - a
                                 : which == 1 ? a * 2.5L
                                 : which == 2 ? tier0_test_x87_third(a).x + 1.0L
                                 : which == 3 ? tier0_test_x87_sum(2, a, b)
                                              : a * 5.0L + tier0_test_x87_mix(a, a);
    return expected;
}

// The functions, each compiled and run from C on the four argument pairs, and each result what C computes:
//   - f(a, i, b) = (a + b) * i - a: two long double parameters on the stack around an integer one in a register;
//   - g(a) = (struct){ a * 2.5 }: a struct of a long double returned in st0;
//   - k(a) = third(a).x + 1: a call whose struct result comes back in st0 into its object;
//   - v(a, b) = sum(2, a, b) = 2a + b: long doubles to a variadic function, on the stack;
//   - w(a) = a * 5 + mix(a, a): a long double in its slot across a call.
static void tier0_test_x87_functions(Tier0TestTally* tally, const Tier0TestX87Abi* abi, Tier0TestX87Shared* shared)
{
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, image != 0 && arena.base != 0, "x87", "no memory");
    for (uint32_t which = 0; which < 5 && ready; which += 1)
    {
        static const char* const names[5] = { "f", "g", "k", "v", "w" };
        static const uint32_t signatures[5] = { BUSTER_TIER0_TEST_X87_F, BUSTER_TIER0_TEST_X87_G, BUSTER_TIER0_TEST_X87_K, BUSTER_TIER0_TEST_X87_V, BUSTER_TIER0_TEST_X87_K };
        static const Tier0TestX87Run runs[5] = { tier0_test_x87_run_f, tier0_test_x87_run_g, tier0_test_x87_run_one, tier0_test_x87_run_two, tier0_test_x87_run_one };
        static const long double constants[5] = { 0.0L, 2.5L, 1.0L, 0.0L, 5.0L };
        Tier0TestX87Written written;
        memset(&written, 0, sizeof(written));
        tier0_test_x87_bits(constants[which], &written.constants[0], &written.constants[1]);
        uint32_t result_size = 0;
        tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
        written.parameter_count = 1;
        if (which == 0)
        {
            tier0_test_x87_local(&written, 1, BUSTER_TYPED_CLASS_I64);
            tier0_test_x87_local(&written, 2, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 3;
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
        }
        else if (which == 1)
        {
            tier0_test_x87_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            result_size = 16;
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (which == 2)
        {
            static const uint8_t classes[1] = { BUSTER_TYPED_CLASS_F80 };
            tier0_test_x87_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_THIRD, classes, 1, 2);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_third);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
        }
        else if (which == 3)
        {
            static const uint8_t classes[3] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
            tier0_test_x87_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 2;
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_SUM, classes, 3, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_sum);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 2);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
        }
        else
        {
            static const uint8_t classes[2] = { BUSTER_TYPED_CLASS_F80, BUSTER_TYPED_CLASS_F80 };
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_MIX, classes, 2, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_mix);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
        }
        arena_pop_to(&arena, 0);
        const Tier0Function function = tier0_test_x87_written_function(&written, abi, signatures[which], result_size);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_x87_encode(tally, names[which], &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, false, &error);
        const bool ran = length != 0 && tier0_test_expect(tally, tier0_test_x87_native(image, length, runs[which], shared), "x87", "%s: the native run failed", names[which]);
        for (uint32_t k = 0; k < 4 && ran; k += 1)
        {
            const long double expected = tier0_test_x87_expected(which, k);
            tier0_test_expect(tally, tier0_test_x87_same(shared->results[k], expected), "x87", "%s: arguments %u: %La, %La expected", names[which], k, shared->results[k],
                              expected);
        }
    }
    arena_release(&arena);
    free(image);
}

// ---------------------------------------------------------------------------------------------------------------------
// The boundaries

// The control word each call of the boundaries' cases starts under: rounding upward, which a conversion to an integer must
// not use (it truncates) and must leave as it was (it restores the word it changed), 64-bit precision, every exception
// masked.
#define BUSTER_TIER0_TEST_X87_UPWARD 0x0b7f
// The default control word, which the run puts back.
#define BUSTER_TIER0_TEST_X87_DEFAULT_CONTROL 0x037f

// Calls the boundaries' conversion (shared->boundary_signature) on each case, each under BUSTER_TIER0_TEST_X87_UPWARD,
// recording its result and the control word after it.
static void tier0_test_x87_run_boundary(const void* code, Tier0TestX87Shared* shared)
{
    typedef uint64_t (*ToInteger)(long double);
    typedef long double (*FromInteger)(uint64_t);
    const uint16_t upward = BUSTER_TIER0_TEST_X87_UPWARD;
    const uint16_t original = BUSTER_TIER0_TEST_X87_DEFAULT_CONTROL;
    const uint32_t signature = shared->boundary_signature;
    const bool to_integer = signature == BUSTER_TIER0_TEST_X87_TO_U64 || signature == BUSTER_TIER0_TEST_X87_TO_I64 || signature == BUSTER_TIER0_TEST_X87_TO_I32;
    ToInteger to = 0;
    FromInteger from = 0;
    memcpy(&to, &code, sizeof(to));
    memcpy(&from, &code, sizeof(from));
    for (uint32_t k = 0; k < shared->boundary_count; k += 1)
    {
        uint16_t control = 0;
        __asm__ volatile("fldcw %0" : : "m"(upward) : "memory");
        if (to_integer)
        {
            // An int result is eax's: the upper half of rax isn't the function's.
            const uint64_t result = to(shared->boundary_values[k]);
            shared->boundary_integers[k] = signature == BUSTER_TIER0_TEST_X87_TO_I32 ? (uint64_t)(int64_t)(int32_t)result : result;
        }
        else
        {
            shared->boundary_values[k] = from(shared->boundary_integers[k]);
        }
        __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
        shared->boundary_controls[k] = control;
    }
    __asm__ volatile("fldcw %0" : : "m"(original) : "memory");
}

// Each conversion between long double and the 64-bit integers that has its own sequence, as Tier 0 emits it alone, on
// named cases rather than random ones: unsigned long at 0 and 1, below 2^63 (by 1 and by a half), at it and past it (by
// 1 and to 10^19), and its largest value both ways; long and int at their limits and on fractions of either sign. Each
// result must be C's conversion of the same value, every case in range (C leaves the others undefined), and each call,
// started under rounding upward, must truncate and leave the control word as it was.
static void tier0_test_x87_boundaries(Tier0TestTally* tally, const Tier0TestX87Abi* abi, Tier0TestX87Shared* shared)
{
    static const long double unsigned_values[] = { 0.0L, 0.75L, 1.0L, 0x1p63L - 1.0L, 0x1p63L - 0.5L, 0x1p63L, 0x1p63L + 1.0L, 1e19L, 0x1p64L - 1.0L };
    static const long double signed_values[] = { -0x1p63L, -0x1p63L + 0.5L, 0x1p63L - 1.0L, 0x1p63L - 1.5L, -1.75L, 1.75L, -0.5L, 0.0L };
    static const long double int_values[] = { 2147483647.0L, 2147483647.75L, -2147483648.0L, -2147483648.75L, -1.5L, 1.5L, 0.999L };
    static const uint64_t unsigned_integers[] = { 0, 1, (UINT64_C(1) << 63) - 1, UINT64_C(1) << 63, (UINT64_C(1) << 63) + 1, UINT64_MAX - 1, UINT64_MAX };
    static const uint64_t signed_integers[] = { (uint64_t)INT64_MIN, (uint64_t)(INT64_MIN + 1), UINT64_MAX, 0, (uint64_t)INT64_MAX };
    static const char* const names[] = { "to unsigned long", "to long", "to int", "from unsigned long", "from long" };
    static const uint8_t classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64 };
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, image != 0 && arena.base != 0, "x87", "no memory");
    for (uint32_t which = 0; which < BUSTER_ARRAY_LENGTH(names) && ready; which += 1)
    {
        const uint32_t signature = BUSTER_TIER0_TEST_X87_TO_U64 + which;
        const bool to_integer = which < 3;
        const uint32_t class = classes[which];
        Tier0TestX87Written written;
        memset(&written, 0, sizeof(written));
        written.parameter_count = 1;
        if (to_integer)
        {
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONVERT, class, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, class, 0);
        }
        else
        {
            tier0_test_x87_local(&written, 0, class);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL, class, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
        }
        const long double* values = which == 0 ? unsigned_values : which == 1 ? signed_values : int_values;
        const uint32_t value_count = which == 0 ? BUSTER_ARRAY_LENGTH(unsigned_values) : which == 1 ? BUSTER_ARRAY_LENGTH(signed_values) : BUSTER_ARRAY_LENGTH(int_values);
        const uint64_t* integers = which == 3 ? unsigned_integers : signed_integers;
        const uint32_t integer_count = which == 3 ? BUSTER_ARRAY_LENGTH(unsigned_integers) : BUSTER_ARRAY_LENGTH(signed_integers);
        shared->boundary_signature = signature;
        shared->boundary_count = to_integer ? value_count : integer_count;
        for (uint32_t k = 0; k < shared->boundary_count; k += 1)
        {
            shared->boundary_values[k] = to_integer ? values[k] : 0.0L;
            shared->boundary_integers[k] = to_integer ? 0 : integers[k];
            shared->boundary_controls[k] = 0;
        }
        arena_pop_to(&arena, 0);
        const Tier0Function function = tier0_test_x87_written_function(&written, abi, signature, 0);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_x87_encode(tally, names[which], &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, false, &error);
        const bool ran = length != 0 &&
                         tier0_test_expect(tally, tier0_test_x87_native(image, length, tier0_test_x87_run_boundary, shared), "x87", "%s: the native run failed", names[which]);
        for (uint32_t k = 0; k < shared->boundary_count && ran; k += 1)
        {
            if (to_integer)
            {
                const long double value = values[k];
                const uint64_t expected = which == 0 ? (uint64_t)value : which == 1 ? (uint64_t)(int64_t)value : (uint64_t)(int64_t)(int32_t)value;
                tier0_test_expect(tally, shared->boundary_integers[k] == expected, "x87", "%s: %La gave %#" PRIx64 ", %#" PRIx64 " expected", names[which], value,
                                  shared->boundary_integers[k], expected);
            }
            else
            {
                const long double expected = which == 3 ? (long double)integers[k] : (long double)(int64_t)integers[k];
                tier0_test_expect(tally, tier0_test_x87_same(shared->boundary_values[k], expected), "x87", "%s: %#" PRIx64 " gave %La, %La expected", names[which],
                                  integers[k], shared->boundary_values[k], expected);
            }
            tier0_test_expect(tally, shared->boundary_controls[k] == BUSTER_TIER0_TEST_X87_UPWARD, "x87", "%s: case %u left the control word %#x, %#x expected",
                              names[which], k, shared->boundary_controls[k], BUSTER_TIER0_TEST_X87_UPWARD);
        }
    }
    arena_release(&arena);
    free(image);
}

// ---------------------------------------------------------------------------------------------------------------------
// The rules

// A break of the class's rules, refused at entry `node`, or (`node` UINT32_MAX) a stream it takes.
typedef struct Tier0TestX87Break Tier0TestX87Break;
struct Tier0TestX87Break
{
    const char* name;
    uint32_t node;
};

// Each break of the stream's rules for long double, at its entry, then the valid ones beside them: a value local of the
// class, a global read or stored as one, a constant past the column, an integer operation of it, a branch on it, a
// return of it from a function whose result isn't one or of another class from one whose is, a call's x87 result left on
// the x87 stack, a long double argument in a register, and a function returning a complex long double (st0 and st1).
static void tier0_test_x87_rules(Tier0TestTally* tally, const Tier0TestX87Abi* abi)
{
    static const Tier0TestX87Break breaks[] = {
        { "a value local of the class", 0 },
        { "GLOBAL", 0 },
        { "a constant past the column", 0 },
        { "SET_GLOBAL", 1 },
        { "AND", 2 },
        { "NOT", 1 },
        { "a branch on it", 1 },
        { "a return from a function whose result isn't one", 1 },
        { "an I32's return from one whose is", 1 },
        { "a call's x87 result left", 2 },
        { "an argument in a register", 0 },
        { "a complex result", 0 },
        { "constants without their column", 0 },
        { "valid: a long double's return", UINT32_MAX },
        { "valid: a call's long double", UINT32_MAX },
        { "an entry of the class without x87", 0 },
        { "a long double result without x87", 0 },
        { "LOCAL of a value local", 0 },
        { "LOCAL of an object of 8 bytes", 0 },
        { "valid: LOCAL, STORE_LOCAL and SET_LOCAL of objects of 16 and 24 bytes", UINT32_MAX },
    };
    const uint64_t significand = 1ull << 63;
    const uint64_t constants[2] = { significand, 0x3fff };
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(breaks); b += 1)
    {
        Tier0TestX87Written written;
        memset(&written, 0, sizeof(written));
        memcpy(written.constants, constants, sizeof(constants));
        uint32_t signature = BUSTER_TIER0_TEST_X87_BODY;
        uint32_t constant_count = 1;
        AbiReturn result = abi->calls[BUSTER_TIER0_TEST_X87_BODY].result;
        static const uint8_t one[1] = { BUSTER_TYPED_CLASS_F80 };
        switch (b)
        {
        case 0:
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_F80);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 1:
            tier0_test_x87_put(&written, BUSTER_TYPED_GLOBAL, BUSTER_TYPED_CLASS_F80, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 2:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 3:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_SET_GLOBAL, BUSTER_TYPED_CLASS_F80, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 4:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 5:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_NOT, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 6:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_F80, 2);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 7:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
            break;
        case 8:
            signature = BUSTER_TIER0_TEST_X87_K;
            result = abi->calls[signature].result;
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 1;
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I32, 0);
            break;
        case 9:
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_K, one, 1, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_mix);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 10:
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_K, one, 1, 0);
            written.argument_locations[0] = (AbiLocation){ .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } };
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_mix);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 11:
            result = (AbiReturn){ .kind = BUSTER_ABI_RESULT_REGISTERS, .part_bytes = 16,
                                  .registers = { BUSTER_ABI_REGISTER_X87, BUSTER_ABI_REGISTER_X87 | 1, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE } };
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 12:
            constant_count = 2;
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 13:
            signature = BUSTER_TIER0_TEST_X87_K;
            result = abi->calls[signature].result;
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 1;
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F80, 0);
            break;
        case 15:
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 17:
        case 18:
            // A U64's local, and an object with room for less than the value.
            tier0_test_x87_local(&written, 0, b == 17 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID);
            written.local_sizes[0] = b == 17 ? 0 : 8;
            written.local_alignments[0] = b == 17 ? 0 : 3;
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 19:
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            tier0_test_x87_local(&written, 1, BUSTER_TYPED_CLASS_VOID);
            written.local_sizes[1] = 24;
            written.local_alignments[1] = 3;
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_F80, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F80, 1);
            tier0_test_x87_put(&written, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 16:
            // A long double result and no entry of the class: a loop that never returns.
            signature = BUSTER_TIER0_TEST_X87_K;
            result = abi->calls[signature].result;
            tier0_test_x87_local(&written, 0, BUSTER_TYPED_CLASS_VOID);
            written.parameter_count = 1;
            tier0_test_x87_put(&written, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        default:
            tier0_test_x87_written_call(&written, abi, BUSTER_TIER0_TEST_X87_K, one, 1, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_x87_mix);
            tier0_test_x87_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F80, 0);
            tier0_test_x87_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        }
        Tier0Function function = tier0_test_x87_written_function(&written, abi, signature, 0);
        function.result = result;
        function.x87_constant_count = constant_count;
        // The body's signature has four parameters; a break that takes none keeps no local.
        function.parameter_count = written.parameter_count;
        function.parameters = written.parameter_count != 0 ? abi->locations + abi->first[signature] : 0;
        if (b == 12)
        {
            function.x87_constants = 0;
        }
        // Long double's values in a function that doesn't say it has them, the last two breaks'.
        function.x87 = tier0_test_x87_has(&function) && b != 15 && b != 16;
        const Tier0Check check = tier0_validate(&function);
        const bool expected = breaks[b].node == UINT32_MAX ? check.error == TIER0_OK : check.error == TIER0_ERROR_INVALID && check.node == breaks[b].node;
        tier0_test_expect(tally, expected, "x87", "%s: tier0_validate says %s at %u (%s)", breaks[b].name, check.error == TIER0_OK ? "valid" : "invalid", check.node,
                          check.rule != 0 ? check.rule : "");
    }
}

// The x87 check: the rules, the functions called from C and calling C, the conversions' boundaries, then `count` random
// programs.
static void tier0_test_x87(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, uint32_t* too_deep, uint64_t* entries)
{
    Tier0TestX87Abi abi;
    Tier0TestX87Shared* shared = mmap(0, sizeof(Tier0TestX87Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const bool ready = tier0_test_expect(tally, shared != MAP_FAILED && tier0_test_x87_abi(tables, &abi), "x87", "no memory, or the signatures' types");
    if (ready)
    {
        tier0_test_x87_rules(tally, &abi);
        tier0_test_x87_functions(tally, &abi, shared);
        tier0_test_x87_boundaries(tally, &abi, shared);
        tier0_test_x87_programs(tally, &abi, seed, count, shared, too_deep, entries);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestX87Shared));
    }
}
