// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 0's tests (test-tier0): generated programs (tier0_generate.c) compiled by both paths, which must agree on every
// per-entry column and every record; their records valid for x86_validate and their branch columns for
// branch_sizing_validate; then sized, encoded, loaded into memory and run in a child process, which must give what an
// interpreter of the typed stream gives, the result and the globals' bytes, for random inputs. With --cross-check, the
// same programs printed as C and compiled by clang -O0 are linked beside Tier 0's objects (written by the object writer)
// into one executable, which runs both on the same inputs and compares them, and their code's sizes are compared.
//
// Usage: build/compiler test-tier0 [--seed N] [--count N] [--cross-check] [--clang PATH] [--keep]
//
// Layout, in this order: the tally; the tables (types and the calling convention) and the functions of a program
// (tier0_test_functions); the interpreter (tier0_test_interpret); compiling, sizing and encoding a program
// (tier0_test_build); the native run in a child process (tier0_test_run); the checks; the cross-check; the command.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

// ---------------------------------------------------------------------------------------------------------------------
// The tally

typedef struct Tier0TestTally Tier0TestTally;
struct Tier0TestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool tier0_test_expect(Tier0TestTally* tally, bool condition, const char* check, const char* format, ...) __attribute__((format(printf, 4, 5)));
static bool tier0_test_expect(Tier0TestTally* tally, bool condition, const char* check, const char* format, ...)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            va_list arguments;
            va_start(arguments, format);
            fprintf(stderr, "test-tier0: %s: ", check);
            vfprintf(stderr, format, arguments);
            fprintf(stderr, "\n");
            va_end(arguments);
        }
        tally->failures += 1;
    }
    return condition;
}

static void tier0_test_report(const char* check, Tier0TestTally tally)
{
    printf("%-34s %10" PRIu64 " checks, %" PRIu64 " failed\n", check, tally.checks, tally.failures);
}

// ---------------------------------------------------------------------------------------------------------------------
// The functions of a program

// A class's builtin type, for the calling convention.
static TypeId tier0_test_class_type(uint32_t class)
{
    static const TypeId types[BUSTER_TYPED_CLASSES] = {
        BUSTER_TYPE_VOID, BUSTER_TYPE_SCHAR, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_USHORT, BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_FLOAT, BUSTER_TYPE_DOUBLE,
    };
    return types[class < BUSTER_TYPED_CLASSES ? class : 0];
}

// The calling convention's tables, for the programs' signatures.
typedef struct Tier0TestTables Tier0TestTables;
struct Tier0TestTables
{
    TypeTable types;
    Arena type_arena;
    Arena index_arena;
    AbiTable abi;
    Arena abi_arena;
};

static bool tier0_test_tables_open(Tier0TestTables* tables)
{
    const TypeCapacity capacity = {
        .types = 256,
        .parameters = 1024,
        .tags = 16,
        .members = 64,
    };
    tables->type_arena = arena_reserve(type_capacity_bytes(capacity));
    tables->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    bool result = type_initialize(&tables->types, &tables->type_arena, &tables->index_arena, capacity);
    tables->abi_arena = arena_reserve(abi_capacity_bytes(tables->types.types.capacity));
    result = result && abi_initialize(&tables->abi, &tables->abi_arena, &tables->types, 16);
    return result;
}

static void tier0_test_tables_close(Tier0TestTables* tables)
{
    arena_release(&tables->type_arena);
    arena_release(&tables->index_arena);
    arena_release(&tables->abi_arena);
}

// A program's functions as Tier 0 takes them, with every signature's locations from one abi_assign batch: each
// function's own (its parameters and result) and each call's (its callee's).
typedef struct Tier0TestProgram Tier0TestProgram;
struct Tier0TestProgram
{
    Tier0Function functions[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    uint32_t count;
    // SymbolIds: globals 1 to global_count, then the functions.
    SymbolId* call_targets;
    AbiLocation* parameter_locations;
    AbiLocation* argument_locations;
    AbiCall* entries;
    AbiCall* calls;
    uint8_t* call_variadic;
};

static bool tier0_test_functions(const Tier0Program* program, const Tier0TestTables* tables, Arena* arena, Tier0TestProgram* functions)
{
    const uint32_t function_count = program->function_count;
    const uint32_t signatures = function_count + program->call_count;
    uint32_t parameters = 0;
    for (uint32_t f = 0; f < function_count; f += 1)
    {
        parameters += program->functions[f].parameter_count;
    }
    const uint32_t arguments = parameters + program->argument_count;
    TypeId* results = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, signatures + 1);
    uint32_t* counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, signatures + 1);
    uint8_t* variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, signatures + 1);
    TypeId* types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, arguments + 1);
    AbiLocation* locations = BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, arguments + 1);
    AbiCall* calls = BUSTER_ARENA_PUSH_ARRAY(arena, AbiCall, signatures + 1);
    functions->call_targets = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, program->call_count + 1);
    functions->call_variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, program->call_count + 1);
    bool result = results != 0 && counts != 0 && variadic != 0 && types != 0 && locations != 0 && calls != 0 && functions->call_targets != 0 &&
                  functions->call_variadic != 0;
    if (result)
    {
        uint32_t argument = 0;
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            const Tier0GeneratedFunction* function = &program->functions[f];
            results[f] = tier0_test_class_type(function->result_class);
            counts[f] = function->parameter_count;
            variadic[f] = 0;
            for (uint32_t p = 0; p < function->parameter_count; p += 1)
            {
                types[argument] = tier0_test_class_type(program->local_classes[function->first_local + p]);
                argument += 1;
            }
        }
        for (uint32_t c = 0; c < program->call_count; c += 1)
        {
            const Tier0GeneratedFunction* callee = &program->functions[program->call_callees[c]];
            results[function_count + c] = tier0_test_class_type(callee->result_class);
            counts[function_count + c] = program->call_argument_counts[c];
            variadic[function_count + c] = 0;
            for (uint32_t a = 0; a < program->call_argument_counts[c]; a += 1)
            {
                types[argument] = tier0_test_class_type(program->argument_classes[program->call_first[c] + a]);
                argument += 1;
            }
            functions->call_targets[c] = program->global_count + 1 + program->call_callees[c];
            functions->call_variadic[c] = 0;
        }
        const AbiSignatures batch = {
            .results = results,
            .counts = counts,
            .named = counts,
            .variadic = variadic,
            .count = signatures,
            .arguments = types,
        };
        abi_assign(&tables->abi, batch, locations, calls);
        // Each function's columns are its ranges of the program's; its calls' arguments index the program's argument
        // columns, which start after every function's parameters in the batch.
        functions->argument_locations = locations + parameters;
        functions->calls = calls + function_count;
        uint32_t parameter = 0;
        for (uint32_t f = 0; f < function_count; f += 1)
        {
            const Tier0GeneratedFunction* function = &program->functions[f];
            Tier0Function* t = &functions->functions[f];
            memset(t, 0, sizeof(*t));
            t->kinds = program->kinds + function->first_entry;
            t->classes = program->classes + function->first_entry;
            t->values = program->values + function->first_entry;
            t->count = function->entry_count;
            t->local_classes = program->local_classes + function->first_local;
            t->local_flags = program->local_flags + function->first_local;
            t->local_count = function->local_count;
            t->parameter_count = function->parameter_count;
            t->parameters = locations + parameter;
            t->result = calls[f].result;
            parameter += function->parameter_count;
            t->call_targets = functions->call_targets + function->first_call;
            t->call_first = program->call_first + function->first_call;
            t->call_argument_counts = program->call_argument_counts + function->first_call;
            t->call_variadic = functions->call_variadic + function->first_call;
            t->calls = functions->calls + function->first_call;
            t->argument_locations = functions->argument_locations;
            t->argument_classes = program->argument_classes;
            t->call_count = function->call_count;
            t->argument_count = program->argument_count;
        }
        functions->count = function_count;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The interpreter

// The globals' memory: each global at an offset (8-aligned), as the loader lays them out, so that the interpreter and
// the native run hash the same bytes.
typedef struct Tier0TestData Tier0TestData;
struct Tier0TestData
{
    uint32_t offsets[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t size;
};

static Tier0TestData tier0_test_layout(const Tier0Program* program)
{
    Tier0TestData data;
    memset(&data, 0, sizeof(data));
    uint32_t offset = 0;
    for (uint32_t g = 0; g < program->global_count; g += 1)
    {
        data.offsets[g] = offset;
        offset += (tier0_class_bytes(program->global_classes[g]) * program->global_elements[g] + 7) & ~7u;
    }
    data.size = offset;
    return data;
}

// The globals' first values into `memory`.
static void tier0_test_initial(const Tier0Program* program, const Tier0TestData* data, uint8_t* memory)
{
    memset(memory, 0, data->size);
    for (uint32_t g = 0; g < program->global_count; g += 1)
    {
        const uint32_t bytes = tier0_class_bytes(program->global_classes[g]);
        for (uint32_t e = 0; e < program->global_elements[g]; e += 1)
        {
            const uint64_t value = program->global_values[program->global_first_value[g] + e];
            memcpy(memory + data->offsets[g] + e * bytes, &value, bytes);
        }
    }
}

// A value of a class from its bits: an integer sign- or zero-extended from its width, a float's bits zero-extended.
static uint64_t tier0_test_normalize(uint32_t class, uint64_t value)
{
    uint64_t result = value;
    switch (class)
    {
    case BUSTER_TYPED_CLASS_I8:
        result = (uint64_t)(int64_t)(int8_t)value;
        break;
    case BUSTER_TYPED_CLASS_U8:
        result = (uint8_t)value;
        break;
    case BUSTER_TYPED_CLASS_I16:
        result = (uint64_t)(int64_t)(int16_t)value;
        break;
    case BUSTER_TYPED_CLASS_U16:
        result = (uint16_t)value;
        break;
    case BUSTER_TYPED_CLASS_I32:
        result = (uint64_t)(int64_t)(int32_t)value;
        break;
    case BUSTER_TYPED_CLASS_U32:
    case BUSTER_TYPED_CLASS_F32:
        result = (uint32_t)value;
        break;
    default:
        break;
    }
    return result;
}

static uint64_t tier0_test_load(uint32_t class, uint64_t address)
{
    uint64_t value = 0;
    memcpy(&value, (const void*)(uintptr_t)address, tier0_class_bytes(class));
    return tier0_test_normalize(class, value);
}

static void tier0_test_store(uint32_t class, uint64_t address, uint64_t value)
{
    memcpy((void*)(uintptr_t)address, &value, tier0_class_bytes(class));
}

static double tier0_test_double(uint32_t class, uint64_t bits)
{
    double d = 0.0;
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        float f = 0.0f;
        const uint32_t low = (uint32_t)bits;
        memcpy(&f, &low, sizeof(f));
        d = (double)f;
    }
    else
    {
        memcpy(&d, &bits, sizeof(d));
    }
    return d;
}

static uint64_t tier0_test_bits(uint32_t class, double d)
{
    uint64_t bits = 0;
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        const float f = (float)d;
        uint32_t low = 0;
        memcpy(&low, &f, sizeof(low));
        bits = low;
    }
    else
    {
        memcpy(&bits, &d, sizeof(bits));
    }
    return bits;
}

// A conversion as Tier 0 makes it (and C, but for floating values out of an integer's range, which cvttsd2si makes the
// integer indefinite).
static uint64_t tier0_test_convert(uint32_t to, uint32_t from, uint64_t value)
{
    uint64_t result = 0;
    const bool to_floating = tier0_class_floating(to);
    const bool from_floating = tier0_class_floating(from);
    if (!to_floating && !from_floating)
    {
        result = tier0_test_normalize(to, value);
    }
    else if (to_floating && !from_floating)
    {
        const double d = tier0_class_signed(from) ? (double)(int64_t)value : (double)value;
        // A float from an integer rounds once, from the integer: as cvtsi2ss does.
        if (to == BUSTER_TYPED_CLASS_F32)
        {
            const float f = tier0_class_signed(from) ? (float)(int64_t)value : (float)value;
            uint32_t bits = 0;
            memcpy(&bits, &f, sizeof(bits));
            result = bits;
        }
        else
        {
            result = tier0_test_bits(to, d);
        }
    }
    else if (!to_floating && from_floating)
    {
        const bool wide = tier0_class_bytes(to) == 8 || to == BUSTER_TYPED_CLASS_U32;
        if (from == BUSTER_TYPED_CLASS_F32)
        {
            const __m128 v = _mm_castsi128_ps(_mm_cvtsi32_si128((int32_t)(uint32_t)value));
            result = wide ? (uint64_t)_mm_cvttss_si64(v) : (uint64_t)(int64_t)_mm_cvttss_si32(v);
        }
        else
        {
            const __m128d v = _mm_castsi128_pd(_mm_cvtsi64_si128((int64_t)value));
            result = wide ? (uint64_t)_mm_cvttsd_si64(v) : (uint64_t)(int64_t)_mm_cvttsd_si32(v);
        }
        result = tier0_test_normalize(to, result);
    }
    else
    {
        result = tier0_test_bits(to, tier0_test_double(from, value));
    }
    return result;
}

static uint64_t tier0_test_binary(uint32_t kind, uint32_t class, uint64_t a, uint64_t b, bool* fault)
{
    uint64_t result = 0;
    if (tier0_class_floating(class))
    {
        double x = tier0_test_double(class, a);
        double y = tier0_test_double(class, b);
        if (class == BUSTER_TYPED_CLASS_F32)
        {
            const float fx = (float)x;
            const float fy = (float)y;
            const float r = kind == BUSTER_TYPED_ADD ? fx + fy : kind == BUSTER_TYPED_SUB ? fx - fy : kind == BUSTER_TYPED_MUL ? fx * fy : fx / fy;
            result = tier0_test_bits(class, (double)r);
        }
        else
        {
            const double r = kind == BUSTER_TYPED_ADD ? x + y : kind == BUSTER_TYPED_SUB ? x - y : kind == BUSTER_TYPED_MUL ? x * y : x / y;
            result = tier0_test_bits(class, r);
        }
    }
    else
    {
        const bool wide = tier0_class_bytes(class) == 8;
        const bool is_signed = tier0_class_signed(class);
        const uint32_t mask = wide ? 63 : 31;
        switch (kind)
        {
        case BUSTER_TYPED_ADD:
            result = a + b;
            break;
        case BUSTER_TYPED_SUB:
            result = a - b;
            break;
        case BUSTER_TYPED_MUL:
            result = a * b;
            break;
        case BUSTER_TYPED_AND:
            result = a & b;
            break;
        case BUSTER_TYPED_OR:
            result = a | b;
            break;
        case BUSTER_TYPED_XOR:
            result = a ^ b;
            break;
        case BUSTER_TYPED_SHL:
            result = a << (b & mask);
            break;
        case BUSTER_TYPED_SHR:
            result = is_signed ? (uint64_t)((int64_t)a >> (b & mask)) : (wide ? a : (uint32_t)a) >> (b & mask);
            break;
        default:
        {
            // DIV and MOD of normalized operands: as the class's width says.
            const int64_t sa = wide ? (int64_t)a : (int64_t)(int32_t)a;
            const int64_t sb = wide ? (int64_t)b : (int64_t)(int32_t)b;
            const uint64_t ua = wide ? a : (uint32_t)a;
            const uint64_t ub = wide ? b : (uint32_t)b;
            *fault = ub == 0 || (is_signed && sb == -1);
            if (!*fault)
            {
                result = kind == BUSTER_TYPED_DIV ? (is_signed ? (uint64_t)(sa / sb) : ua / ub) : (is_signed ? (uint64_t)(sa % sb) : ua % ub);
            }
            break;
        }
        }
        result = tier0_test_normalize(class, result);
    }
    return result;
}

static uint64_t tier0_test_compare(uint32_t kind, uint32_t class, uint64_t a, uint64_t b)
{
    bool holds = false;
    if (tier0_class_floating(class))
    {
        const double x = tier0_test_double(class, a);
        const double y = tier0_test_double(class, b);
        holds = kind == BUSTER_TYPED_EQ ? x == y : kind == BUSTER_TYPED_NE ? x != y : kind == BUSTER_TYPED_LT ? x < y : kind == BUSTER_TYPED_LE ? x <= y : kind == BUSTER_TYPED_GT ? x > y : x >= y;
    }
    else if (tier0_class_signed(class))
    {
        const int64_t x = (int64_t)a;
        const int64_t y = (int64_t)b;
        holds = kind == BUSTER_TYPED_EQ ? x == y : kind == BUSTER_TYPED_NE ? x != y : kind == BUSTER_TYPED_LT ? x < y : kind == BUSTER_TYPED_LE ? x <= y : kind == BUSTER_TYPED_GT ? x > y : x >= y;
    }
    else
    {
        holds = kind == BUSTER_TYPED_EQ ? a == b : kind == BUSTER_TYPED_NE ? a != b : kind == BUSTER_TYPED_LT ? a < b : kind == BUSTER_TYPED_LE ? a <= b : kind == BUSTER_TYPED_GT ? a > b : a >= b;
    }
    return holds ? 1 : 0;
}

// A frame of the interpreter: the function, where it is, its locals' slots and its stack's base.
typedef struct Tier0TestFrame Tier0TestFrame;
struct Tier0TestFrame
{
    uint32_t function;
    uint32_t entry;
    uint64_t* locals;
    uint32_t stack_base;
};

#define BUSTER_TIER0_TEST_FRAMES 256
#define BUSTER_TIER0_TEST_STACK 4096
#define BUSTER_TIER0_TEST_SLOTS (BUSTER_TIER0_TEST_FRAMES * BUSTER_TIER0_GENERATE_MAX_LOCALS)
#define BUSTER_TIER0_TEST_STEPS 200000000ull

// Runs the program's entry (its last function) on three arguments over the globals at `globals` (the data layout's),
// and returns its result; `fault` is set on a division the generator should have made safe, or past the step budget.
static uint64_t tier0_test_interpret(const Tier0Program* program, const Tier0TestProgram* functions, const Tier0TestData* data, uint8_t* globals,
                                     const uint64_t* inputs, bool* fault)
{
    static Tier0TestFrame frames[BUSTER_TIER0_TEST_FRAMES];
    static uint64_t stack[BUSTER_TIER0_TEST_STACK];
    static uint64_t slots[BUSTER_TIER0_TEST_SLOTS];
    uint32_t depth = 1;
    uint32_t top = 0;
    uint64_t result = 0;
    uint64_t steps = 0;
    const uint32_t entry = program->function_count - 1;
    frames[0] = (Tier0TestFrame){ entry, 0, slots, 0 };
    memset(slots, 0, sizeof(uint64_t) * BUSTER_TIER0_GENERATE_MAX_LOCALS);
    for (uint32_t p = 0; p < 3 && p < functions->functions[entry].parameter_count; p += 1)
    {
        slots[p] = inputs[p];
    }
    *fault = false;
    while (depth != 0 && !*fault)
    {
        Tier0TestFrame* frame = &frames[depth - 1];
        const Tier0Function* f = &functions->functions[frame->function];
        steps += 1;
        if (frame->entry >= f->count)
        {
            // Off the end of a void function.
            depth -= 1;
            top = frame->stack_base;
            continue;
        }
        const uint32_t i = frame->entry;
        frame->entry += 1;
        const uint32_t kind = f->kinds[i];
        const uint32_t class = f->classes[i];
        const uint32_t operand = i > 0 ? f->classes[i - 1] : 0;
        const uint64_t value = f->values[i];
        switch (kind)
        {
        case BUSTER_TYPED_CONST:
            stack[top++] = value;
            break;
        case BUSTER_TYPED_LOCAL:
            stack[top++] = tier0_test_load(class, (uint64_t)(uintptr_t)&frame->locals[value]);
            break;
        case BUSTER_TYPED_LOCAL_ADDRESS:
            stack[top++] = (uint64_t)(uintptr_t)&frame->locals[value];
            break;
        case BUSTER_TYPED_GLOBAL:
            stack[top++] = tier0_test_load(class, (uint64_t)(uintptr_t)(globals + data->offsets[value - 1]));
            break;
        case BUSTER_TYPED_GLOBAL_ADDRESS:
            stack[top++] = (uint64_t)(uintptr_t)(globals + data->offsets[value - 1]);
            break;
        case BUSTER_TYPED_DUP:
            stack[top] = stack[top - 1];
            top += 1;
            break;
        case BUSTER_TYPED_NEG:
            stack[top - 1] = tier0_class_floating(class) ? stack[top - 1] ^ (class == BUSTER_TYPED_CLASS_F32 ? 0x80000000ull : 0x8000000000000000ull)
                                                         : tier0_test_normalize(class, 0 - stack[top - 1]);
            break;
        case BUSTER_TYPED_NOT:
            stack[top - 1] = tier0_test_normalize(class, ~stack[top - 1]);
            break;
        case BUSTER_TYPED_CONVERT:
            stack[top - 1] = tier0_test_convert(class, operand, stack[top - 1]);
            break;
        case BUSTER_TYPED_LOAD:
            stack[top - 1] = tier0_test_load(class, stack[top - 1]);
            break;
        case BUSTER_TYPED_PTR_ADD:
            top -= 1;
            stack[top - 1] += stack[top] * value;
            break;
        case BUSTER_TYPED_EQ:
        case BUSTER_TYPED_NE:
        case BUSTER_TYPED_LT:
        case BUSTER_TYPED_LE:
        case BUSTER_TYPED_GT:
        case BUSTER_TYPED_GE:
            top -= 1;
            stack[top - 1] = tier0_test_compare(kind, operand, stack[top - 1], stack[top]);
            break;
        case BUSTER_TYPED_STORE:
        case BUSTER_TYPED_SET:
            top -= 2;
            tier0_test_store(class, stack[top], stack[top + 1]);
            if (kind == BUSTER_TYPED_STORE)
            {
                stack[top] = stack[top + 1];
                top += 1;
            }
            break;
        case BUSTER_TYPED_STORE_LOCAL:
        case BUSTER_TYPED_SET_LOCAL:
            tier0_test_store(class, (uint64_t)(uintptr_t)&frame->locals[value], stack[top - 1]);
            top -= kind == BUSTER_TYPED_SET_LOCAL ? 1 : 0;
            break;
        case BUSTER_TYPED_STORE_GLOBAL:
        case BUSTER_TYPED_SET_GLOBAL:
            tier0_test_store(class, (uint64_t)(uintptr_t)(globals + data->offsets[value - 1]), stack[top - 1]);
            top -= kind == BUSTER_TYPED_SET_GLOBAL ? 1 : 0;
            break;
        case BUSTER_TYPED_CALL:
        {
            const uint32_t callee = program->call_callees[program->functions[frame->function].first_call + value];
            const uint32_t arguments = f->call_argument_counts[value];
            if (depth >= BUSTER_TIER0_TEST_FRAMES)
            {
                *fault = true;
                break;
            }
            uint64_t* locals = frame->locals + BUSTER_TIER0_GENERATE_MAX_LOCALS;
            memset(locals, 0, sizeof(uint64_t) * BUSTER_TIER0_GENERATE_MAX_LOCALS);
            top -= arguments;
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                locals[a] = stack[top + a];
            }
            frames[depth] = (Tier0TestFrame){ callee, 0, locals, top };
            depth += 1;
            break;
        }
        case BUSTER_TYPED_DISCARD:
            top -= 1;
            break;
        case BUSTER_TYPED_BRANCH_FALSE:
        case BUSTER_TYPED_BRANCH_TRUE:
            top -= 1;
            if ((stack[top] != 0) == (kind == BUSTER_TYPED_BRANCH_TRUE))
            {
                frame->entry = (uint32_t)value;
            }
            break;
        case BUSTER_TYPED_JUMP:
        case BUSTER_TYPED_JUMP_VALUE:
            frame->entry = (uint32_t)value;
            break;
        case BUSTER_TYPED_RETURN:
        {
            const uint64_t returned = class != BUSTER_TYPED_CLASS_VOID ? stack[top - 1] : 0;
            depth -= 1;
            top = frame->stack_base;
            if (depth != 0 && class != BUSTER_TYPED_CLASS_VOID)
            {
                stack[top++] = returned;
            }
            result = returned;
            break;
        }
        default:
            top -= 1;
            stack[top - 1] = tier0_test_binary(kind, class, stack[top - 1], stack[top], fault);
            break;
        }
        *fault |= steps > BUSTER_TIER0_TEST_STEPS || top + 64 > BUSTER_TIER0_TEST_STACK;
    }
    return result;
}

static uint64_t tier0_test_hash(const uint8_t* bytes, uint64_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint64_t b = 0; b < length; b += 1)
    {
        hash = (hash ^ bytes[b]) * 0x100000001b3ull;
    }
    return hash;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compiling, sizing and encoding a program

#define BUSTER_TIER0_TEST_CODE_BYTES (8u << 20)
#define BUSTER_TIER0_TEST_FIXUPS (1u << 16)

// A program's machine code (each function at its offset, 16-aligned) and its fixups: each field's offset in the code,
// its instruction's end and its target symbol.
typedef struct Tier0TestCode Tier0TestCode;
struct Tier0TestCode
{
    uint8_t* code;
    uint32_t offsets[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS + 1];
    uint32_t size;
    uint32_t fields[BUSTER_TIER0_TEST_FIXUPS];
    uint32_t ends[BUSTER_TIER0_TEST_FIXUPS];
    SymbolId targets[BUSTER_TIER0_TEST_FIXUPS];
    uint32_t fixup_count;
    uint64_t records;
    uint64_t entries;
    uint64_t deferred;
};

// Whether two compilations of a function made the same code: records, branch columns and references.
static bool tier0_test_same_code(const Tier0Code* a, const Tier0Code* b)
{
    bool same = a->error == b->error && a->count == b->count && a->branch_count == b->branch_count && a->reference_count == b->reference_count &&
                a->deferred == b->deferred;
    same = same && memcmp(a->records, b->records, a->count * sizeof(X86Instruction)) == 0;
    same = same && memcmp(a->branches, b->branches, a->branch_count * sizeof(uint32_t)) == 0;
    same = same && memcmp(a->targets, b->targets, a->branch_count * sizeof(uint32_t)) == 0;
    for (uint32_t r = 0; same && r < a->reference_count; r += 1)
    {
        const LinkReference* x = &a->references[r];
        const LinkReference* y = &b->references[r];
        same = x->instruction == y->instruction && x->field == y->field && x->kind == y->kind && x->target == y->target && x->addend == y->addend;
    }
    return same;
}

// Compiles every function of the program by both paths (which must agree), checks the records and branch columns,
// sizes the branches and encodes each function into `code`.
static bool tier0_test_build(Tier0TestTally* tally, const Tier0TestProgram* functions, Arena* arena, Tier0TestCode* code, uint64_t seed)
{
    bool result = true;
    code->size = 0;
    code->fixup_count = 0;
    for (uint32_t f = 0; f < functions->count && result; f += 1)
    {
        const Tier0Function* function = &functions->functions[f];
        const uint64_t mark = arena->length;
        const Tier0Check check = tier0_validate(function);
        result = tier0_test_expect(tally, check.error == TIER0_OK, "streams", "seed %" PRIu64 " function %u: entry %u breaks a rule: %s", seed, f, check.node,
                                   check.rule != 0 ? check.rule : "");
        Tier0Code scalar = result ? tier0_compile_scalar(function, arena) : (Tier0Code){ 0 };
        Tier0Code vector = result ? tier0_compile(function, arena) : (Tier0Code){ 0 };
        result = result && tier0_test_expect(tally, scalar.error == TIER0_OK, "compile", "seed %" PRIu64 " function %u: error %d at entry %u", seed, f, scalar.error,
                                             scalar.node);
        result = result && tier0_test_expect(tally, tier0_test_same_code(&scalar, &vector), "twins", "seed %" PRIu64 " function %u: the paths differ", seed, f);
        code->records += scalar.count;
        code->entries += function->count;
        code->deferred += scalar.deferred;
        if (result)
        {
            const uint64_t valid = x86_validate(scalar.records, scalar.count);
            result = tier0_test_expect(tally, valid == scalar.count, "records", "seed %" PRIu64 " function %u: record %" PRIu64 " of %u is invalid", seed, f, valid,
                                       scalar.count);
        }
        if (result)
        {
            const BranchSizingCheck sizing = branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count);
            result = tier0_test_expect(tally, sizing.error == BRANCH_SIZING_OK, "branches", "seed %" PRIu64 " function %u: error %d at %" PRIu64, seed, f, sizing.error,
                                       sizing.index);
        }
        uint32_t* offsets = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
        uint32_t* ends = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(scalar.reference_count)) : 0;
        if (result && offsets != 0 && ends != 0)
        {
            const BranchSizing sizing = size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets);
            const uint32_t start = (code->size + 15) & ~15u;
            const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
            result = tier0_test_expect(tally, sizing.error == BRANCH_SIZING_OK && start + capacity <= BUSTER_TIER0_TEST_CODE_BYTES &&
                                                  code->fixup_count + scalar.reference_count <= BUSTER_TIER0_TEST_FIXUPS,
                                       "encode", "seed %" PRIu64 " function %u: sizing error %d or no room", seed, f, sizing.error);
            const int64_t length = result ? x86_encode(scalar.records, scalar.count, code->code + start, capacity, 0, ends) : -1;
            result = result && tier0_test_expect(tally, length >= 0, "encode", "seed %" PRIu64 " function %u: x86_encode failed", seed, f);
            if (result)
            {
                // The k-th reference is the k-th marked record's: one symbolic field each.
                for (uint32_t r = 0; r < scalar.reference_count; r += 1)
                {
                    const LinkReference* reference = &scalar.references[r];
                    const uint32_t end = ends[r];
                    const uint32_t field = end - x86_fixup_distance(scalar.records[reference->instruction].form, reference->field);
                    code->fields[code->fixup_count] = start + field;
                    code->ends[code->fixup_count] = start + end;
                    code->targets[code->fixup_count] = reference->target;
                    code->fixup_count += 1;
                }
                code->offsets[f] = start;
                code->size = start + (uint32_t)length;
            }
        }
        else
        {
            result = false;
        }
        arena_pop_to(arena, mark);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The native run

#define BUSTER_TIER0_TEST_INPUTS 8
#define BUSTER_TIER0_TEST_PAGE 4096u

typedef struct Tier0TestOutcome Tier0TestOutcome;
struct Tier0TestOutcome
{
    uint64_t result;
    uint64_t globals;
};

// The inputs of a program's runs: random values, some small.
static void tier0_test_inputs(uint64_t seed, uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3])
{
    uint64_t state = seed ^ 0x5eedull;
    for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
    {
        for (uint32_t a = 0; a < 3; a += 1)
        {
            const uint64_t value = tier0_generate_random(&state);
            inputs[r][a] = r < 4 ? value % 7 : value;
        }
    }
}

// Loads the code and the globals into fresh pages, patches the fixups, and runs the entry on every input in a child
// process, whose outcomes come back through shared memory. Returns false, with `status` the child's, if it didn't
// exit normally.
static bool tier0_test_run(const Tier0Program* program, const Tier0TestCode* code, const Tier0TestData* data, uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3],
                           Tier0TestOutcome* outcomes, int* status)
{
    const uint64_t code_pages = ((uint64_t)code->size + BUSTER_TIER0_TEST_PAGE - 1) / BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_PAGE + BUSTER_TIER0_TEST_PAGE;
    const uint64_t data_pages = ((uint64_t)data->size + BUSTER_TIER0_TEST_PAGE - 1) / BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_PAGE + BUSTER_TIER0_TEST_PAGE;
    Tier0TestOutcome* shared = mmap(0, sizeof(Tier0TestOutcome) * BUSTER_TIER0_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    bool result = shared != MAP_FAILED;
    const pid_t child = result ? fork() : -1;
    if (child == 0)
    {
        uint8_t* region = mmap(0, code_pages + data_pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (region == MAP_FAILED)
        {
            _exit(3);
        }
        uint8_t* data_base = region + code_pages;
        memcpy(region, code->code, code->size);
        for (uint32_t x = 0; x < code->fixup_count; x += 1)
        {
            const SymbolId target = code->targets[x];
            const uint8_t* address = target <= program->global_count ? data_base + data->offsets[target - 1] : region + code->offsets[target - program->global_count - 1];
            const int32_t displacement = (int32_t)(address - (region + code->ends[x]));
            memcpy(region + code->fields[x], &displacement, sizeof(displacement));
        }
        if (mprotect(region, code_pages, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        typedef uint64_t (*Tier0TestEntry)(uint64_t, uint64_t, uint64_t);
        Tier0TestEntry entry = 0;
        void* entry_address = region + code->offsets[program->function_count - 1];
        memcpy(&entry, &entry_address, sizeof(entry));
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            tier0_test_initial(program, data, data_base);
            shared[r].result = entry(inputs[r][0], inputs[r][1], inputs[r][2]);
            shared[r].globals = tier0_test_hash(data_base, data->size);
        }
        _exit(0);
    }
    if (child > 0)
    {
        waitpid(child, status, 0);
        result = WIFEXITED(*status) && WEXITSTATUS(*status) == 0;
        memcpy(outcomes, shared, sizeof(Tier0TestOutcome) * BUSTER_TIER0_TEST_INPUTS);
    }
    else
    {
        result = false;
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestOutcome) * BUSTER_TIER0_TEST_INPUTS);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

// Programs from consecutive seeds: generated, compiled by both paths, run natively and by the interpreter, compared.
static void tier0_test_programs(Tier0TestTally* tally, Tier0TestTables* tables, uint64_t seed, uint32_t count, Tier0ProgramOptions options, uint64_t* records,
                                uint64_t* entries, uint64_t* deferred)
{
    Arena arena = arena_reserve(1ull << 32);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
    if (tier0_test_expect(tally, code != 0 && code_bytes != 0 && globals != 0, "programs", "no memory"))
    {
        code->code = code_bytes;
        for (uint32_t p = 0; p < count; p += 1)
        {
            const uint64_t program_seed = seed + p;
            const uint64_t mark = arena.length;
            Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
            Tier0TestProgram* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestProgram, 1);
            bool ok = program != 0 && functions != 0 && tier0_generate_program(program, &arena, program_seed, options, "c_");
            ok = tier0_test_expect(tally, ok, "programs", "seed %" PRIu64 ": the generator ran out of room", program_seed);
            ok = ok && tier0_test_expect(tally, tier0_test_functions(program, tables, &arena, functions), "programs", "seed %" PRIu64 ": no room for the signatures",
                                         program_seed);
            code->records = 0;
            code->entries = 0;
            code->deferred = 0;
            ok = ok && tier0_test_build(tally, functions, &arena, code, program_seed);
            *records += code->records;
            *entries += code->entries;
            *deferred += code->deferred;
            if (ok)
            {
                const Tier0TestData data = tier0_test_layout(program);
                uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
                tier0_test_inputs(program_seed, inputs);
                Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
                int status = 0;
                const bool ran = tier0_test_run(program, code, &data, inputs, native, &status);
                ok = tier0_test_expect(tally, ran, "runs", "seed %" PRIu64 ": the program didn't exit normally (status %d, signal %d)", program_seed, status,
                                       WIFSIGNALED(status) ? WTERMSIG(status) : 0);
                for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
                {
                    tier0_test_initial(program, &data, globals);
                    bool fault = false;
                    const uint64_t expected = tier0_test_interpret(program, functions, &data, globals, inputs[r], &fault);
                    const uint64_t hash = tier0_test_hash(globals, data.size);
                    ok = tier0_test_expect(tally, !fault, "runs", "seed %" PRIu64 " input %u: the interpreter faulted", program_seed, r);
                    ok = ok && tier0_test_expect(tally, native[r].result == expected && native[r].globals == hash, "runs",
                                                 "seed %" PRIu64 " input %u: %016" PRIx64 " (globals %016" PRIx64 "), the interpreter %016" PRIx64 " (%016" PRIx64 ")",
                                                 program_seed, r, native[r].result, native[r].globals, expected, hash);
                }
            }
            arena_pop_to(&arena, mark);
        }
    }
    arena_release(&arena);
}

static int run_tier0_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint32_t count = 300;
    bool valid = true;
    for (int a = 0; a < argc && valid; a += 1)
    {
        if (strcmp(argv[a], "--seed") == 0 && a + 1 < argc)
        {
            seed = strtoull(argv[a + 1], 0, 0);
            a += 1;
        }
        else if (strcmp(argv[a], "--count") == 0 && a + 1 < argc)
        {
            count = (uint32_t)strtoul(argv[a + 1], 0, 0);
            a += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: build/compiler test-tier0 [--seed N] [--count N]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-tier0: the tables can't be built\n");
    }
    else
    {
        Tier0TestTables tables;
        Tier0TestTally total = { 0, 0 };
        if (tier0_test_tables_open(&tables))
        {
            static const Tier0ProgramOptions shapes[4] = {
                { .functions = 1, .statements = 6, .expression_nodes = 6, .calls = false, .loops = false, .floating = false, .pointers = false },
                { .functions = 3, .statements = 12, .expression_nodes = 10, .calls = true, .loops = true, .floating = false, .pointers = false },
                { .functions = 4, .statements = 16, .expression_nodes = 14, .calls = true, .loops = true, .floating = true, .pointers = true },
                { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true },
            };
            static const char* const names[4] = { "programs: one function", "programs: integers and calls", "programs: everything", "programs: larger" };
            for (uint32_t s = 0; s < 4; s += 1)
            {
                Tier0TestTally tally = { 0, 0 };
                uint64_t records = 0;
                uint64_t entries = 0;
                uint64_t deferred = 0;
                tier0_test_programs(&tally, &tables, seed + s * 1000003ull, count, shapes[s], &records, &entries, &deferred);
                tier0_test_report(names[s], tally);
                printf("    %" PRIu64 " entries, %" PRIu64 " records, %" PRIu64 " deferred\n", entries, records, deferred);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            tier0_test_tables_close(&tables);
        }
        else
        {
            total.failures += 1;
        }
        printf("test-tier0: %" PRIu64 " checks, %" PRIu64 " failed\n", total.checks, total.failures);
        result = total.failures == 0 ? 0 : 1;
    }
    return result;
}
