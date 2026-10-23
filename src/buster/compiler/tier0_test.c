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
// (tier0_program_functions); the interpreter (tier0_test_interpret); compiling, sizing and encoding a program
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

// A frame of the interpreter: the function, where it is, its locals' slots, its objects' memory and its stack's base.
typedef struct Tier0TestFrame Tier0TestFrame;
struct Tier0TestFrame
{
    uint32_t function;
    uint32_t entry;
    uint64_t* locals;
    uint8_t* objects;
    uint32_t stack_base;
};

#define BUSTER_TIER0_TEST_FRAMES 256
#define BUSTER_TIER0_TEST_STACK 4096
#define BUSTER_TIER0_TEST_SLOTS (BUSTER_TIER0_TEST_FRAMES * BUSTER_TIER0_GENERATE_MAX_LOCALS)
// A frame's objects' bytes, each object at a multiple of 16 (the generator's take a few hundred at most).
#define BUSTER_TIER0_TEST_OBJECT_BYTES 2048
// A function's address in the interpreter: its index under this tag.
#define BUSTER_TIER0_TEST_FUNCTION_TAG 0xf0f0000000000000ull
#define BUSTER_TIER0_TEST_STEPS 200000000ull

// Runs the program's entry (its last function) on three arguments over the globals at `globals` (the data layout's),
// and returns its result; `fault` is set on a division the generator should have made safe, past the step budget, or
// on an indirect call that reaches a function of another type.
static uint64_t tier0_test_interpret(const Tier0Program* program, const Tier0Functions* functions, const Tier0TestData* data, uint8_t* globals,
                                     const uint64_t* inputs, bool* fault)
{
    static Tier0TestFrame frames[BUSTER_TIER0_TEST_FRAMES];
    static uint64_t stack[BUSTER_TIER0_TEST_STACK];
    static uint64_t slots[BUSTER_TIER0_TEST_SLOTS];
    static uint8_t objects[BUSTER_TIER0_TEST_FRAMES * BUSTER_TIER0_TEST_OBJECT_BYTES] __attribute__((aligned(64)));
    // Each object's offset in its frame's memory, by the program's local index.
    static uint32_t object_offsets[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS * BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t depth = 1;
    uint32_t top = 0;
    uint64_t result = 0;
    uint64_t steps = 0;
    const uint32_t entry = program->function_count - 1;
    *fault = false;
    for (uint32_t f = 0; f < program->function_count; f += 1)
    {
        const Tier0GeneratedFunction* function = &program->functions[f];
        uint32_t offset = 0;
        for (uint32_t l = 0; l < function->local_count; l += 1)
        {
            const uint32_t size = program->local_sizes != 0 ? program->local_sizes[function->first_local + l] : 0;
            object_offsets[function->first_local + l] = offset;
            offset += (size + 15) & ~15u;
        }
        *fault |= offset > BUSTER_TIER0_TEST_OBJECT_BYTES;
    }
    frames[0] = (Tier0TestFrame){ entry, 0, slots, objects, 0 };
    memset(slots, 0, sizeof(uint64_t) * BUSTER_TIER0_GENERATE_MAX_LOCALS);
    memset(objects, 0, BUSTER_TIER0_TEST_OBJECT_BYTES);
    for (uint32_t p = 0; p < 3 && p < functions->functions[entry].parameter_count; p += 1)
    {
        slots[p] = inputs[p];
    }
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
            stack[top++] = tier0_is_object(f, value) ? (uint64_t)(uintptr_t)(frame->objects + object_offsets[program->functions[frame->function].first_local + value])
                                                     : (uint64_t)(uintptr_t)&frame->locals[value];
            break;
        case BUSTER_TYPED_GLOBAL:
            stack[top++] = tier0_test_load(class, (uint64_t)(uintptr_t)(globals + data->offsets[value - 1]));
            break;
        case BUSTER_TYPED_GLOBAL_ADDRESS:
            // A function's address is its index under a tag, which only an indirect call reads.
            stack[top++] = value <= program->global_count ? (uint64_t)(uintptr_t)(globals + data->offsets[value - 1])
                                                          : BUSTER_TIER0_TEST_FUNCTION_TAG | (value - 1 - program->global_count);
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
            uint32_t callee = program->call_callees[program->functions[frame->function].first_call + value];
            const uint32_t arguments = f->call_argument_counts[value];
            if (depth >= BUSTER_TIER0_TEST_FRAMES)
            {
                *fault = true;
                break;
            }
            uint64_t* locals = frame->locals + BUSTER_TIER0_GENERATE_MAX_LOCALS;
            uint8_t* callee_objects = frame->objects + BUSTER_TIER0_TEST_OBJECT_BYTES;
            memset(locals, 0, sizeof(uint64_t) * BUSTER_TIER0_GENERATE_MAX_LOCALS);
            memset(callee_objects, 0, BUSTER_TIER0_TEST_OBJECT_BYTES);
            top -= arguments;
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                locals[a] = stack[top + a];
            }
            if (f->call_targets[value] == 0)
            {
                // An indirect call's callee, under its arguments: a function's tagged index, the one the pointer holds
                // when the call runs. Its type must be the call's, checked here apart from the generator's choice of
                // candidates: its result's class the call's, and each parameter's class its argument's.
                top -= 1;
                const uint64_t address = stack[top];
                const uint32_t held = (uint32_t)(address & 0xffff);
                const bool function_held = (address & ~0xffffull) == BUSTER_TIER0_TEST_FUNCTION_TAG && held < program->function_count;
                const Tier0GeneratedFunction* target = &program->functions[function_held ? held : callee];
                bool typed = function_held && target->parameter_count == arguments && target->result_class == class;
                for (uint32_t a = 0; a < arguments && typed; a += 1)
                {
                    typed = program->local_classes[target->first_local + a] == f->argument_classes[f->call_first[value] + a];
                }
                *fault |= !typed;
                callee = function_held ? held : callee;
            }
            frames[depth] = (Tier0TestFrame){ callee, 0, locals, callee_objects, top };
            depth += 1;
            break;
        }
        case BUSTER_TYPED_DISCARD:
            top -= 1;
            break;
        case BUSTER_TYPED_COPY:
            top -= 2;
            memmove((void*)(uintptr_t)stack[top], (const void*)(uintptr_t)stack[top + 1], value);
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
    uint32_t lengths[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS + 1];
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
static bool tier0_test_build(Tier0TestTally* tally, const Tier0Functions* functions, Arena* arena, Tier0TestCode* code, uint64_t seed)
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
                code->lengths[f] = (uint32_t)length;
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
#define BUSTER_TIER0_TEST_RUN_SECONDS 10

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
        // A program runs for milliseconds; code that loops (a frame laid out wrong can overwrite a loop's counter) is
        // stopped by the alarm, a failure like a crash.
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
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
static void tier0_test_programs(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, Tier0ProgramOptions options, uint64_t* records,
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
            Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Functions, 1);
            bool ok = program != 0 && functions != 0 && tier0_generate_program(program, &arena, program_seed, options, "c_");
            ok = tier0_test_expect(tally, ok, "programs", "seed %" PRIu64 ": the generator ran out of room", program_seed);
            ok = ok && tier0_test_expect(tally, tier0_program_functions(program, tables, &arena, functions), "programs", "seed %" PRIu64 ": no room for the signatures",
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
                    ok = tier0_test_expect(tally, !fault, "runs",
                                           "seed %" PRIu64 " input %u: the interpreter faulted (a division, its steps, or an indirect call of another type)",
                                           program_seed, r);
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

// ---------------------------------------------------------------------------------------------------------------------
// The cross-check

// Tier 0's code of a program as an ELF object: a TEXT atom per function and a DATA atom per global (its first values),
// named t0_f<k>, t0_entry and t0_g<k> (symbols in the streams' order: the globals, then the functions), with a fixup
// per symbolic field: CALL for a call, PC32 for a global's address or value, each addend the field's distance from its
// instruction's end taken off, as link_add_code_fixups makes them.
static bool tier0_test_object(const Tier0Program* program, const Tier0TestCode* code, const char* path)
{
    const uint32_t globals = program->global_count;
    const uint32_t functions = program->function_count;
    const LinkCapacity capacity = {
        .names = globals + functions + 4,
        .atoms = globals + functions + 4,
        .symbols = globals + functions + 4,
        .fixups = code->fixup_count + 4,
        .wide_addends = 2,
        .priorities = 2,
    };
    Arena tables = arena_reserve(link_capacity_bytes(capacity));
    Arena names = arena_reserve(64ull * capacity.names + 4096);
    Arena bytes = arena_reserve(BUSTER_TIER0_TEST_CODE_BYTES + 65536);
    Arena objects = arena_reserve(4 * (uint64_t)BUSTER_TIER0_TEST_CODE_BYTES);
    Link link;
    bool result = tables.base != 0 && names.base != 0 && bytes.base != 0 && objects.base != 0 && link_initialize(&link, &tables, &names, capacity);
    for (uint32_t g = 0; result && g < globals; g += 1)
    {
        char name[32];
        const int length = snprintf(name, sizeof(name), "t0_g%u", g);
        result = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
                                 BUSTER_LINK_SOURCE_PROGRAM) == g + 1;
    }
    for (uint32_t f = 0; result && f < functions; f += 1)
    {
        char name[32];
        const int length = f + 1 == functions ? snprintf(name, sizeof(name), "t0_entry") : snprintf(name, sizeof(name), "t0_f%u", f);
        result = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL,
                                 BUSTER_LINK_SOURCE_PROGRAM) == globals + 1 + f;
    }
    // The functions' atoms, each with its fixups (the code's fixups are in function order).
    uint32_t fixup = 0;
    for (uint32_t f = 0; result && f < functions; f += 1)
    {
        const uint32_t start = code->offsets[f];
        const uint32_t length = code->lengths[f];
        uint8_t* atom_bytes = arena_push(&bytes, length + 1, 16);
        result = atom_bytes != 0;
        if (result)
        {
            memcpy(atom_bytes, code->code + start, length);
            const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, length, atom_bytes, 0);
            result = atom != 0 && link_define_symbol(&link, globals + 1 + f, atom, 0, length);
            while (result && fixup < code->fixup_count && code->fields[fixup] < start + length)
            {
                const SymbolId target = code->targets[fixup];
                const uint8_t kind = target > globals ? BUSTER_LINK_FIXUP_CALL : BUSTER_LINK_FIXUP_PC32;
                const int64_t addend = -(int64_t)(code->ends[fixup] - code->fields[fixup]);
                // The field's bytes are the fixup's, zero as GNU as writes them.
                memset(atom_bytes + (code->fields[fixup] - start), 0, 4);
                result = link_add_fixup(&link, atom, code->fields[fixup] - start, kind, target, addend) != 0;
                fixup += 1;
            }
        }
    }
    for (uint32_t g = 0; result && g < globals; g += 1)
    {
        const uint32_t class_bytes = tier0_class_bytes(program->global_classes[g]);
        const uint32_t size = class_bytes * program->global_elements[g];
        uint8_t* atom_bytes = arena_push(&bytes, size, 8);
        result = atom_bytes != 0;
        if (result)
        {
            for (uint32_t e = 0; e < program->global_elements[g]; e += 1)
            {
                memcpy(atom_bytes + e * class_bytes, &program->global_values[program->global_first_value[g] + e], class_bytes);
            }
            const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, size, atom_bytes, 0);
            result = atom != 0 && link_define_symbol(&link, g + 1, atom, 0, size);
        }
    }
    ObjectUnits units;
    ObjectSymbols object_symbols;
    result = result && link_check(&link) == LINK_ERROR_NONE && object_prepare(&units, &link, 1, &objects) && object_map_symbols(&object_symbols, &link, &objects);
    if (result)
    {
        const ObjectOptions options = {
            .function_sections = false,
        };
        const Object object = object_build(&link, &units, &object_symbols, 0, options, BUSTER_S8("tier0.c"), &objects);
        result = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
    }
    arena_release(&tables);
    arena_release(&names);
    arena_release(&bytes);
    arena_release(&objects);
    return result;
}

// The harness: both programs' globals saved at start and put back before each run, every input run by clang's build and
// by Tier 0's, the results and every global compared, then each build timed over the inputs.
static bool tier0_test_harness(const Tier0Program* program, uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3], const char* path)
{
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    if (result)
    {
        fprintf(file, "#include <stdio.h>\n#include <string.h>\n#include <time.h>\n\n");
        fprintf(file, "unsigned long c_entry(unsigned long, unsigned long, unsigned long);\nunsigned long t0_entry(unsigned long, unsigned long, unsigned long);\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const char* type = tier0_c_type(program->global_classes[g]);
            const char* array = program->global_elements[g] != 1 ? "[16]" : "";
            fprintf(file, "extern %s c_g%u%s;\nextern %s t0_g%u%s;\nstatic unsigned char c_s%u[sizeof c_g%u], t0_s%u[sizeof t0_g%u];\n", type, g, array, type, g, array,
                    g, g, g, g);
        }
        fprintf(file, "static void reset(void)\n{\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            fprintf(file, "    memcpy(&c_g%u, c_s%u, sizeof c_s%u);\n    memcpy(&t0_g%u, t0_s%u, sizeof t0_s%u);\n", g, g, g, g, g, g);
        }
        // A floating global's elements compare as their bytes, but any two NaNs are the same: C leaves a NaN's sign and
        // payload to the implementation, and clang folds a constant 0.0 / 0.0 to +NaN where divsd makes -NaN.
        fprintf(file, "}\n\nstatic int same_double(const double* a, const double* b, unsigned long count)\n{\n    int same = 1;\n"
                      "    for (unsigned long i = 0; i < count; i += 1)\n    {\n"
                      "        same &= memcmp(&a[i], &b[i], sizeof a[i]) == 0 || (a[i] != a[i] && b[i] != b[i]);\n    }\n    return same;\n}\n\n");
        fprintf(file, "static int same_float(const float* a, const float* b, unsigned long count)\n{\n    int same = 1;\n"
                      "    for (unsigned long i = 0; i < count; i += 1)\n    {\n"
                      "        same &= memcmp(&a[i], &b[i], sizeof a[i]) == 0 || (a[i] != a[i] && b[i] != b[i]);\n    }\n    return same;\n}\n");
        fprintf(file, "\nstatic const unsigned long inputs[%u][3] = {\n", BUSTER_TIER0_TEST_INPUTS);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            fprintf(file, "    { 0x%llxul, 0x%llxul, 0x%llxul },\n", (unsigned long long)inputs[r][0], (unsigned long long)inputs[r][1], (unsigned long long)inputs[r][2]);
        }
        fprintf(file, "};\n\nstatic long long now(void)\n{\n    struct timespec t;\n    clock_gettime(CLOCK_MONOTONIC, &t);\n"
                      "    return t.tv_sec * 1000000000ll + t.tv_nsec;\n}\n\nint main(void)\n{\n    int failures = 0;\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            fprintf(file, "    memcpy(c_s%u, &c_g%u, sizeof c_s%u);\n    memcpy(t0_s%u, &t0_g%u, sizeof t0_s%u);\n", g, g, g, g, g, g);
        }
        fprintf(file, "    for (int r = 0; r < %u; r += 1)\n    {\n        reset();\n", BUSTER_TIER0_TEST_INPUTS);
        fprintf(file, "        unsigned long c = c_entry(inputs[r][0], inputs[r][1], inputs[r][2]);\n");
        fprintf(file, "        unsigned long t = t0_entry(inputs[r][0], inputs[r][1], inputs[r][2]);\n");
        fprintf(file, "        if (c != t)\n        {\n            printf(\"mismatch input %%d: clang %%016lx tier0 %%016lx\\n\", r, c, t);\n            failures += 1;\n        }\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const uint32_t class = program->global_classes[g];
            if (tier0_class_floating(class))
            {
                const char* same = class == BUSTER_TYPED_CLASS_F32 ? "same_float" : "same_double";
                fprintf(file, "        if (!%s((const void*)&c_g%u, (const void*)&t0_g%u, sizeof c_g%u / %u))\n", same, g, g, g, tier0_class_bytes(class));
            }
            else
            {
                fprintf(file, "        if (memcmp(&c_g%u, &t0_g%u, sizeof c_g%u) != 0)\n", g, g, g);
            }
            fprintf(file, "        {\n            printf(\"mismatch input %%d: global %u\\n\", r);\n            failures += 1;\n        }\n", g);
        }
        fprintf(file, "    }\n    long long times[2] = { 0, 0 };\n    for (int side = 0; side < 2; side += 1)\n    {\n        long long start = now();\n");
        fprintf(file, "        for (int k = 0; k < 64; k += 1)\n        {\n            for (int r = 0; r < %u; r += 1)\n            {\n                reset();\n", BUSTER_TIER0_TEST_INPUTS);
        fprintf(file, "                (void)(side == 0 ? c_entry : t0_entry)(inputs[r][0], inputs[r][1], inputs[r][2]);\n            }\n        }\n");
        fprintf(file, "        times[side] = now() - start;\n    }\n    printf(\"time %%lld %%lld\\n\", times[0], times[1]);\n    printf(failures == 0 ? \"OK\\n\" : \"FAILED\\n\");\n");
        fprintf(file, "    return failures == 0 ? 0 : 1;\n}\n");
        result = fclose(file) == 0;
    }
    return result;
}

// Runs a shell command, and returns its standard output's first line that starts with `prefix` (in `line`) and its
// exit status.
static int tier0_test_shell(const char* command, const char* prefix, char* line, size_t line_size)
{
    FILE* pipe = popen(command, "r");
    int status = -1;
    if (pipe != 0)
    {
        char buffer[512];
        line[0] = 0;
        while (fgets(buffer, sizeof(buffer), pipe) != 0)
        {
            if (prefix != 0 && strncmp(buffer, prefix, strlen(prefix)) == 0 && line[0] == 0)
            {
                snprintf(line, line_size, "%s", buffer);
            }
            else if (prefix == 0)
            {
                fputs(buffer, stderr);
            }
        }
        status = pclose(pipe);
    }
    return status;
}

// The cross-check: `count` programs, each compiled by clang -O0 (as C) and by Tier 0 (as an object), linked into one
// harness that runs both on the same inputs; their results and globals must agree. Also reports the code's sizes (clang's
// .text, Tier 0's functions) and the harness's times.
static void tier0_test_cross(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, Tier0ProgramOptions options, const char* clang,
                             bool keep)
{
    const char* temporary = getenv("TMPDIR");
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/tier0-cross-XXXXXX", temporary != 0 ? temporary : "/tmp");
    Arena arena = arena_reserve(1ull << 32);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    uint64_t clang_bytes = 0;
    uint64_t tier0_bytes = 0;
    long long clang_time = 0;
    long long tier0_time = 0;
    bool kept = false;
    if (tier0_test_expect(tally, code != 0 && code_bytes != 0 && mkdtemp(directory) != 0, "cross-check", "can't start"))
    {
        code->code = code_bytes;
        for (uint32_t p = 0; p < count; p += 1)
        {
            const uint64_t program_seed = seed + 7000001ull + p;
            const uint64_t mark = arena.length;
            Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
            Tier0Functions* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Functions, 1);
            bool ok = program != 0 && functions != 0 && tier0_generate_program(program, &arena, program_seed, options, "c_") &&
                      tier0_program_functions(program, tables, &arena, functions);
            ok = tier0_test_expect(tally, ok, "cross-check", "seed %" PRIu64 ": the generator ran out of room", program_seed);
            ok = ok && tier0_test_build(tally, functions, &arena, code, program_seed);
            char source[600];
            char object[600];
            char harness[600];
            char executable[600];
            snprintf(source, sizeof(source), "%s/program_%u.c", directory, p);
            snprintf(object, sizeof(object), "%s/tier0_%u.o", directory, p);
            snprintf(harness, sizeof(harness), "%s/harness_%u.c", directory, p);
            snprintf(executable, sizeof(executable), "%s/run_%u", directory, p);
            if (ok)
            {
                FILE* file = fopen(source, "w");
                ok = file != 0 && fwrite(program->text, 1, program->text_length, file) == program->text_length;
                ok = file != 0 && fclose(file) == 0 && ok;
                uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
                tier0_test_inputs(program_seed, inputs);
                ok = ok && tier0_test_object(program, code, object) && tier0_test_harness(program, inputs, harness);
                ok = tier0_test_expect(tally, ok, "cross-check", "seed %" PRIu64 ": the files can't be written", program_seed);
            }
            if (ok)
            {
                char command[4096];
                char line[512];
                // -fwrapv and -fno-strict-aliasing, this repository's own dialect: signed arithmetic wraps, as Tier 0's
                // does, and the objects' checksums read floating parts' bits through integer lvalues.
                snprintf(command, sizeof(command), "%s -O0 -fwrapv -fno-strict-aliasing -w -c %s -o %s.o 2>&1 && %s -O0 -w -o %s %s %s.o %s 2>&1", clang, source,
                         source, clang, executable, harness, source, object);
                ok = tier0_test_expect(tally, tier0_test_shell(command, 0, line, sizeof(line)) == 0, "cross-check", "seed %" PRIu64 ": can't compile or link (%s)",
                                       program_seed, directory);
                snprintf(command, sizeof(command), "size -A %s.o", source);
                if (ok && tier0_test_shell(command, ".text ", line, sizeof(line)) == 0)
                {
                    clang_bytes += strtoull(line + 6, 0, 10);
                    for (uint32_t f = 0; f < program->function_count; f += 1)
                    {
                        tier0_bytes += code->lengths[f];
                    }
                }
                snprintf(command, sizeof(command), "%s", executable);
                const int status = ok ? tier0_test_shell(command, "time ", line, sizeof(line)) : -1;
                ok = ok && tier0_test_expect(tally, status == 0, "cross-check", "seed %" PRIu64 ": clang -O0's build and Tier 0's differ (%s)", program_seed,
                                             directory);
                long long times[2] = { 0, 0 };
                if (ok && sscanf(line, "time %lld %lld", &times[0], &times[1]) == 2)
                {
                    clang_time += times[0];
                    tier0_time += times[1];
                }
            }
            kept |= !ok;
            arena_pop_to(&arena, mark);
        }
        printf("    code: clang -O0 %" PRIu64 " bytes, Tier 0 %" PRIu64 " bytes (%.2f); run time: clang -O0 %.2f ms, Tier 0 %.2f ms (%.2f)\n", clang_bytes, tier0_bytes,
               clang_bytes != 0 ? (double)tier0_bytes / (double)clang_bytes : 0.0, (double)clang_time / 1e6, (double)tier0_time / 1e6,
               clang_time != 0 ? (double)tier0_time / (double)clang_time : 0.0);
        if (!keep && !kept)
        {
            char command[700];
            char line[16];
            snprintf(command, sizeof(command), "rm -rf %s", directory);
            (void)tier0_test_shell(command, 0, line, sizeof(line));
        }
        else
        {
            printf("    files kept in %s\n", directory);
        }
    }
    arena_release(&arena);
}

// The argument moves' resolution (tier0_parallel_moves), apart from the depth table that decides which moves a call
// makes (with rax first, only cycles of two): random sets of moves among 16 general registers, each a random injection
// of distinct sources into distinct destinations (paths and cycles of every length, and moves to themselves), and among
// 16 XMM registers without cycles (each source above its destination: xchg has no XMM form, and a call's floating
// arguments only ever move down). The records, run on a register file, must give each destination its source's value and
// leave every other register as it was, and be as many as the moves less the cycles (tier0_count_deferred's count).
static void tier0_test_moves(Tier0TestTally* tally)
{
    uint64_t random = 0x9e3779b97f4a7c15ull;
    X86Instruction records[64];
    for (uint32_t round = 0; round < 20000; round += 1)
    {
        const uint32_t base = round % 2 == 0 ? 0 : 16;
        uint8_t to[16];
        uint8_t from[16];
        uint8_t used_to[16] = { 0 };
        uint8_t used_from[16] = { 0 };
        const uint32_t count = 1 + tier0_generate_below(&random, 16);
        uint32_t moves = 0;
        while (moves < count)
        {
            const uint32_t destination = tier0_generate_below(&random, 16);
            const uint32_t source = round % 7 == 0 ? destination : tier0_generate_below(&random, 16);
            if (used_to[destination] == 0 && used_from[source] == 0 && (base == 0 || source >= destination))
            {
                used_to[destination] = 1;
                used_from[source] = 1;
                to[moves] = (uint8_t)(base + destination);
                from[moves] = (uint8_t)(base + source);
                moves += 1;
            }
            else if (moves == 16 || tier0_generate_below(&random, 8) == 0)
            {
                break;
            }
        }
        // The cycles, found apart: from each move not yet seen, follow the move that writes its source.
        uint32_t cycles = 0;
        uint32_t real = 0;
        uint32_t seen = 0;
        for (uint32_t k = 0; k < moves; k += 1)
        {
            real += to[k] != from[k] ? 1 : 0;
            uint32_t at = k;
            uint32_t steps = 0;
            while (to[k] != from[k] && (seen >> at & 1) == 0 && steps <= moves)
            {
                seen |= 1u << at;
                uint32_t writer = moves;
                for (uint32_t j = 0; j < moves; j += 1)
                {
                    writer = to[j] == from[at] ? j : writer;
                }
                cycles += writer == k ? 1 : 0;
                at = writer == moves ? at : writer;
                steps = writer == moves ? moves + 1 : steps + 1;
            }
        }
        Tier0Writer writer = { records, 0, 0, 0 };
        tier0_parallel_moves(&writer, to, from, moves);
        uint64_t file[32];
        for (uint32_t r = 0; r < 32; r += 1)
        {
            file[r] = 0x1000 + r;
        }
        for (uint32_t r = 0; r < writer.count; r += 1)
        {
            const X86Instruction* record = &records[r];
            const uint32_t reg = base + record->reg;
            const uint32_t rm = base + record->rm;
            const uint64_t swapped = file[reg];
            file[reg] = file[rm];
            file[rm] = record->form == X86_FORM_XCHG_RM64_R64 ? swapped : file[rm];
        }
        bool right = writer.count == real - cycles;
        for (uint32_t r = 0; r < 16; r += 1)
        {
            uint64_t expected = 0x1000 + base + r;
            for (uint32_t k = 0; k < moves; k += 1)
            {
                expected = to[k] == base + r ? 0x1000 + from[k] : expected;
            }
            right &= file[base + r] == expected;
        }
        tier0_test_expect(tally, right, "moves", "round %u: %u moves, %u cycles, %u records", round, moves, cycles, writer.count);
    }
}

// The chosen forms' application, the vector path's (tier0_apply_choices_vector, from a byte per form) against the
// scalar path's (tier0_apply_choice, through x86_choice_instruction): random records, each given a candidate form of a
// random row, or X86_FORM_NONE (which keeps the record), and a random value, in batches of 1 to 64.
static void tier0_test_applies(Tier0TestTally* tally)
{
    uint64_t random = 0x2545f4914f6cdd1dull;
    X86Instruction vector[64];
    X86Instruction scalar[64];
    uint32_t indices[64];
    uint16_t forms[64];
    int64_t values[64];
    for (uint32_t round = 0; round < 2000; round += 1)
    {
        const uint32_t count = 1 + tier0_generate_below(&random, 64);
        for (uint32_t q = 0; q < count; q += 1)
        {
            uint64_t halves[2] = { tier0_generate_random(&random), tier0_generate_random(&random) };
            memcpy(&vector[q], halves, sizeof(vector[q]));
            vector[q].form = (uint16_t)(tier0_generate_below(&random, X86_FORM_COUNT) | (tier0_generate_below(&random, 2) != 0 ? BUSTER_X86_FORM_FIXUP : 0));
            scalar[q] = vector[q];
            const X86ChoiceRow* row = &x86_choice_rows[tier0_generate_below(&random, x86_choice_row_count)];
            const uint32_t candidate = tier0_generate_below(&random, BUSTER_X86_CHOICE_CANDIDATES + 1);
            forms[q] = candidate < BUSTER_X86_CHOICE_CANDIDATES ? row->forms[candidate] : X86_FORM_NONE;
            values[q] = (int64_t)tier0_generate_random(&random) >> tier0_generate_below(&random, 64);
            indices[q] = count - 1 - q;
        }
        Tier0Choices choices;
        memset(&choices, 0, sizeof(choices));
        choices.records = indices;
        choices.forms = forms;
        choices.values = values;
        choices.count = count;
        // Each request's record is at its index; the scalar path's applies in the same order.
        X86Instruction placed[64];
        for (uint32_t q = 0; q < count; q += 1)
        {
            placed[indices[q]] = vector[q];
        }
        tier0_apply_choices_vector(placed, &choices);
        bool same = true;
        for (uint32_t q = 0; q < count; q += 1)
        {
            tier0_apply_choice(&scalar[q], forms[q], values[q]);
            same &= memcmp(&placed[indices[q]], &scalar[q], sizeof(scalar[q])) == 0;
        }
        tier0_test_expect(tally, same, "applies", "round %u: %u requests, a record differs", round, count);
    }
}

// The errors both paths must report alike, on streams of the test's own: the first unsupported entry (a conversion from
// U64 to a floating class, which the first slice leaves out) wherever it falls among 64-entry blocks and after another,
// and an expression deeper than the integer depths.
static void tier0_test_errors(Tier0TestTally* tally)
{
    enum
    {
        capacity = 512,
    };
    uint8_t kinds[capacity];
    uint8_t classes[capacity];
    uint64_t values[capacity];
    Arena arena = arena_reserve(1ull << 28);
    for (uint32_t place = 0; place < 200; place += 1)
    {
        // `place` constants discarded, a conversion that fails, more constants, another, the return.
        uint32_t count = 0;
        uint32_t expected = 0;
        for (uint32_t c = 0; c < 2; c += 1)
        {
            for (uint32_t k = 0; k < (c == 0 ? place : 37); k += 1)
            {
                kinds[count] = BUSTER_TYPED_CONST;
                classes[count] = BUSTER_TYPED_CLASS_I64;
                values[count] = k;
                kinds[count + 1] = BUSTER_TYPED_DISCARD;
                classes[count + 1] = BUSTER_TYPED_CLASS_I64;
                values[count + 1] = 0;
                count += 2;
            }
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_U64;
            values[count] = c + 1;
            kinds[count + 1] = BUSTER_TYPED_CONVERT;
            classes[count + 1] = BUSTER_TYPED_CLASS_F64;
            values[count + 1] = 0;
            kinds[count + 2] = BUSTER_TYPED_DISCARD;
            classes[count + 2] = BUSTER_TYPED_CLASS_F64;
            values[count + 2] = 0;
            expected = c == 0 ? count + 1 : expected;
            count += 3;
        }
        kinds[count] = BUSTER_TYPED_RETURN;
        classes[count] = BUSTER_TYPED_CLASS_VOID;
        values[count] = 0;
        count += 1;
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        const uint64_t mark = arena.length;
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        tier0_test_expect(tally, scalar.error == TIER0_ERROR_UNSUPPORTED && vector.error == scalar.error && scalar.node == expected && vector.node == expected,
                          "errors", "the conversion at entry %u: the scalar path's error %d at %u, the vector path's %d at %u", expected, scalar.error, scalar.node,
                          vector.error, vector.node);
        arena_pop_to(&arena, mark);
    }
    for (uint32_t deep = 14; deep <= 17; deep += 1)
    {
        // `deep` constants on the integer stack at once, added up and discarded. The last one is the first add's
        // immediate, so 16 fit the 15 depths and 17 are refused.
        uint32_t count = 0;
        for (uint32_t k = 0; k < deep; k += 1)
        {
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_I64;
            values[count] = k;
            count += 1;
        }
        for (uint32_t k = 1; k < deep; k += 1)
        {
            kinds[count] = BUSTER_TYPED_ADD;
            classes[count] = BUSTER_TYPED_CLASS_I64;
            values[count] = 0;
            count += 1;
        }
        kinds[count] = BUSTER_TYPED_DISCARD;
        classes[count] = BUSTER_TYPED_CLASS_I64;
        values[count] = 0;
        kinds[count + 1] = BUSTER_TYPED_RETURN;
        classes[count + 1] = BUSTER_TYPED_CLASS_VOID;
        values[count + 1] = 0;
        count += 2;
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        const uint64_t mark = arena.length;
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        const Tier0Error expected = deep > BUSTER_TIER0_INTEGER_DEPTHS + 1 ? TIER0_ERROR_DEPTH : TIER0_OK;
        tier0_test_expect(tally, scalar.error == expected && vector.error == expected && (expected != TIER0_OK || tier0_test_same_code(&scalar, &vector)), "errors",
                          "%u values at once: the scalar path's error %d, the vector path's %d, %d expected", deep, scalar.error, vector.error, expected);
        arena_pop_to(&arena, mark);
    }
    {
        // Past 32 locals, emission gathers each lane's homes; an absorbed constant's lane, whose value is no local,
        // must look none up (a huge value read homes far past their end, which faulted).
        enum
        {
            locals = 40,
        };
        uint8_t local_classes[locals];
        uint8_t local_flags[locals];
        uint32_t count = 0;
        for (uint32_t l = 0; l < locals; l += 1)
        {
            local_classes[l] = BUSTER_TYPED_CLASS_I64;
            local_flags[l] = 0;
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_I64;
            // Each fits the store's imm32 (sign-extended), so that SET_LOCAL absorbs it: as an index, 2^31 homes before
            // them, below the scratch arena (an index as far after them lands in its reserved range, and reads zeros).
            values[count] = (uint64_t)(int64_t)(INT32_MIN + (int32_t)l);
            kinds[count + 1] = BUSTER_TYPED_SET_LOCAL;
            classes[count + 1] = BUSTER_TYPED_CLASS_I64;
            values[count + 1] = l;
            count += 2;
        }
        for (uint32_t l = 0; l < locals; l += 1)
        {
            kinds[count] = BUSTER_TYPED_LOCAL;
            classes[count] = BUSTER_TYPED_CLASS_I64;
            values[count] = l;
            kinds[count + 1] = l == 0 ? BUSTER_TYPED_NONE : BUSTER_TYPED_ADD;
            classes[count + 1] = BUSTER_TYPED_CLASS_I64;
            values[count + 1] = 0;
            count += l == 0 ? 1 : 2;
        }
        kinds[count] = BUSTER_TYPED_RETURN;
        classes[count] = BUSTER_TYPED_CLASS_I64;
        values[count] = 0;
        count += 1;
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = locals;
        const uint64_t mark = arena.length;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        tier0_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "errors",
                          "%u locals set from constants: the paths' errors %d and %d, or their code differs", (uint32_t)locals, scalar.error, vector.error);
        arena_pop_to(&arena, mark);
    }
    arena_release(&arena);
}

// The objects (Tier0Function.local_sizes): the validator's rules, each broken one way on a function that keeps them; then
// random frames of objects and value locals, compiled by both paths, which must agree: each LOCAL_ADDRESS's lea gives
// its local's place, which must be a multiple of the object's alignment, inside the frame, and overlap no other local's
// bytes, and a frame with an object aligned to 16 must leave rsp a multiple of 16 below the pushes and the return
// address.
static void tier0_test_objects(Tier0TestTally* tally, uint64_t seed)
{
    enum
    {
        locals_capacity = 24,
        capacity = 2 * locals_capacity + 1,
    };
    uint8_t kinds[capacity];
    uint8_t classes[capacity];
    uint64_t values[capacity];
    uint8_t local_classes[locals_capacity];
    uint8_t local_flags[locals_capacity];
    uint32_t local_sizes[locals_capacity];
    uint8_t local_alignments[locals_capacity];
    const AbiLocation in_rdi = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RDI } };
    Arena arena = arena_reserve(1ull << 28);
    // The rules: an object of 24 bytes aligned to 8, its address stored through, then each way to break them.
    for (uint32_t way = 0; way < 9; way += 1)
    {
        local_classes[0] = BUSTER_TYPED_CLASS_VOID;
        local_flags[0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        local_sizes[0] = 24;
        local_alignments[0] = 3;
        local_classes[1] = BUSTER_TYPED_CLASS_VOID;
        local_flags[1] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        local_sizes[1] = 16;
        local_alignments[1] = 4;
        kinds[0] = BUSTER_TYPED_LOCAL_ADDRESS;
        classes[0] = BUSTER_TYPED_CLASS_U64;
        values[0] = 0;
        kinds[1] = BUSTER_TYPED_CONST;
        classes[1] = BUSTER_TYPED_CLASS_I32;
        values[1] = 7;
        kinds[2] = BUSTER_TYPED_SET;
        classes[2] = BUSTER_TYPED_CLASS_I32;
        values[2] = 0;
        kinds[3] = BUSTER_TYPED_RETURN;
        classes[3] = BUSTER_TYPED_CLASS_VOID;
        values[3] = 0;
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = 4;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_sizes = local_sizes;
        function.local_alignments = local_alignments;
        function.local_count = 2;
        static const char* const ways[9] = {
            "kept",
            "a class",
            "not address-taken",
            "a parameter",
            "aligned to 32",
            "sizes without alignments",
            "read as a value",
            "past BUSTER_TIER0_MAX_OBJECT_BYTES",
            "BUSTER_TIER0_MAX_OBJECT_BYTES exactly",
        };
        switch (way)
        {
        case 1:
            local_classes[0] = BUSTER_TYPED_CLASS_U64;
            break;
        case 2:
            // The other object is the one stored through, so that LOCAL_ADDRESS's own rule doesn't refuse it first.
            local_flags[0] = 0;
            values[0] = 1;
            break;
        case 3:
            function.parameter_count = 1;
            function.parameters = &in_rdi;
            break;
        case 4:
            local_alignments[0] = 5;
            break;
        case 5:
            function.local_alignments = 0;
            break;
        case 6:
            // LOCAL of the object, then a DISCARD that a VOID breaks too, but later: the object's rule must be first.
            kinds[0] = BUSTER_TYPED_LOCAL;
            classes[0] = BUSTER_TYPED_CLASS_VOID;
            kinds[1] = BUSTER_TYPED_DISCARD;
            classes[1] = BUSTER_TYPED_CLASS_VOID;
            kinds[2] = BUSTER_TYPED_RETURN;
            classes[2] = BUSTER_TYPED_CLASS_VOID;
            function.count = 3;
            break;
        case 7:
            local_sizes[0] = BUSTER_TIER0_MAX_OBJECT_BYTES / 2;
            local_sizes[1] = BUSTER_TIER0_MAX_OBJECT_BYTES / 2 + 1;
            break;
        case 8:
            local_sizes[0] = BUSTER_TIER0_MAX_OBJECT_BYTES / 2;
            local_sizes[1] = BUSTER_TIER0_MAX_OBJECT_BYTES / 2;
            break;
        default:
            break;
        }
        const bool valid = way == 0 || way == 8;
        const Tier0Check check = tier0_validate(&function);
        tier0_test_expect(tally, (check.error == TIER0_OK) == valid && (valid || check.node == 0), "objects", "an object %s: tier0_validate says %s at %u (%s)",
                          ways[way], check.error == TIER0_OK ? "valid" : "invalid", check.node, check.rule != 0 ? check.rule : "");
        if (valid)
        {
            const uint64_t mark = arena.length;
            const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
            const Tier0Code vector = tier0_compile(&function, &arena);
            tier0_test_expect(tally, scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "objects",
                              "an object %s: the paths' errors %d and %d, or their code differs", ways[way], scalar.error, vector.error);
            arena_pop_to(&arena, mark);
        }
    }
    // A copy's rules: between two objects of 256 bytes, all of them, then each way to break them.
    for (uint32_t way = 0; way < 6; way += 1)
    {
        for (uint32_t l = 0; l < 2; l += 1)
        {
            local_classes[l] = BUSTER_TYPED_CLASS_VOID;
            local_flags[l] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            local_sizes[l] = BUSTER_TIER0_MAX_COPY_BYTES;
            local_alignments[l] = 3;
        }
        static const uint8_t copy_kinds[4] = { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_COPY, BUSTER_TYPED_RETURN };
        static const uint8_t copy_classes[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID };
        static const uint64_t copy_values[4] = { 0, 1, BUSTER_TIER0_MAX_COPY_BYTES, 0 };
        memcpy(kinds, copy_kinds, sizeof(copy_kinds));
        memcpy(classes, copy_classes, sizeof(copy_classes));
        memcpy(values, copy_values, sizeof(copy_values));
        static const char* const ways[6] = { "kept", "of no bytes", "past BUSTER_TIER0_MAX_COPY_BYTES", "of a class", "from a constant", "to a constant" };
        switch (way)
        {
        case 1:
            values[2] = 0;
            break;
        case 2:
            values[2] = BUSTER_TIER0_MAX_COPY_BYTES + 1;
            break;
        case 3:
            classes[2] = BUSTER_TYPED_CLASS_U64;
            break;
        case 4:
            kinds[1] = BUSTER_TYPED_CONST;
            classes[1] = BUSTER_TYPED_CLASS_I64;
            break;
        case 5:
            kinds[0] = BUSTER_TYPED_CONST;
            classes[0] = BUSTER_TYPED_CLASS_I64;
            break;
        default:
            break;
        }
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = 4;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_sizes = local_sizes;
        function.local_alignments = local_alignments;
        function.local_count = 2;
        const Tier0Check check = tier0_validate(&function);
        tier0_test_expect(tally, (check.error == TIER0_OK) == (way == 0) && (way == 0 || check.node == 2), "objects", "a copy %s: tier0_validate says %s at %u (%s)",
                          ways[way], check.error == TIER0_OK ? "valid" : "invalid", check.node, check.rule != 0 ? check.rule : "");
    }
    // Random frames: up to 24 locals, objects of 1 to 300 bytes at every alignment to 16, 8-byte values, some
    // address-taken, and VA_LISTs of 24 bytes, all of them address-taken ones read by a LOCAL_ADDRESS each, its lea's
    // displacement its place.
    uint64_t random = seed ^ 0x6f626a656374ull;
    for (uint32_t round = 0; round < 2000; round += 1)
    {
        const uint32_t locals = 1 + tier0_generate_below(&random, locals_capacity);
        uint32_t count = 0;
        bool sixteen = false;
        for (uint32_t l = 0; l < locals; l += 1)
        {
            const bool object = tier0_generate_chance(&random, 60);
            const bool va_list = !object && tier0_generate_chance(&random, 20);
            local_sizes[l] = object ? 1 + tier0_generate_below(&random, 300) : 0;
            local_alignments[l] = object ? (uint8_t)tier0_generate_below(&random, BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 + 1) : 0;
            local_classes[l] = object ? BUSTER_TYPED_CLASS_VOID : va_list ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_I64;
            local_flags[l] = object || tier0_generate_chance(&random, 50) ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
            local_flags[l] = va_list ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST : local_flags[l];
            sixteen |= object && local_alignments[l] == 4;
            if (local_flags[l] != 0)
            {
                kinds[count] = BUSTER_TYPED_LOCAL_ADDRESS;
                classes[count] = BUSTER_TYPED_CLASS_U64;
                values[count] = l;
                kinds[count + 1] = BUSTER_TYPED_DISCARD;
                classes[count + 1] = BUSTER_TYPED_CLASS_U64;
                values[count + 1] = 0;
                count += 2;
            }
        }
        kinds[count] = BUSTER_TYPED_RETURN;
        classes[count] = BUSTER_TYPED_CLASS_VOID;
        values[count] = 0;
        count += 1;
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_sizes = local_sizes;
        function.local_alignments = local_alignments;
        function.local_count = locals;
        const uint64_t mark = arena.length;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        bool laid = check.error == TIER0_OK && scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector);
        // The prologue is the frame's sub alone (nothing is pushed), then a lea per address-taken local, in order.
        const uint32_t size = laid && scalar.count != 0 && (scalar.records[0].form & ~BUSTER_X86_FORM_FIXUP) != X86_FORM_RET ? (uint32_t)scalar.records[0].immediate : 0;
        const uint32_t first = size != 0 ? 1 : 0;
        uint32_t places[locals_capacity];
        uint32_t lea = first;
        for (uint32_t l = 0; l < locals && laid; l += 1)
        {
            if (local_flags[l] != 0)
            {
                // The index first: code with fewer records than leas mustn't make the pointer past them.
                laid &= lea < scalar.count;
                const X86Instruction* record = laid ? &scalar.records[lea] : 0;
                laid = laid && record->form == X86_FORM_LEA_R64_M && record->rm == X86_REGISTER_RSP && record->displacement >= 0;
                places[l] = laid ? (uint32_t)record->displacement : 0;
                lea += 1;
            }
        }
        // An object's bytes are its size, at its alignment; a VA_LIST's are an AbiVaList's, and a value's 8, each at 8.
        uint32_t extents[locals_capacity];
        for (uint32_t l = 0; l < locals; l += 1)
        {
            extents[l] = local_sizes[l] != 0 ? local_sizes[l] : (local_flags[l] & BUSTER_TYPED_LOCAL_VA_LIST) != 0 ? (uint32_t)sizeof(AbiVaList) : 8;
        }
        for (uint32_t l = 0; l < locals && laid; l += 1)
        {
            if (local_flags[l] != 0)
            {
                const uint32_t alignment = local_sizes[l] != 0 ? 1u << local_alignments[l] : 8;
                laid &= places[l] % alignment == 0 && places[l] + extents[l] <= size;
                for (uint32_t other = 0; other < l; other += 1)
                {
                    laid &= local_flags[other] == 0 || places[l] + extents[l] <= places[other] || places[other] + extents[other] <= places[l];
                }
            }
        }
        laid &= !sixteen || (size + 8) % 16 == 0;
        tier0_test_expect(tally, laid, "objects", "round %u: %u locals, a frame of %u bytes: the paths disagree, or a local is misplaced", round, locals, size);
        arena_pop_to(&arena, mark);
    }
    arena_release(&arena);
}

// A copy's scratch register, its stacks' first free one (an XMM one from 16 bytes, a general one below), must be in
// the frame's clobbers after either selection, so that no local makes it its home; past the tables, the function is
// refused as too deep. On copies above every count of integer and floating values, of each size class, with both
// paths' code compared.
static void tier0_test_copies(Tier0TestTally* tally)
{
    enum
    {
        capacity = 64,
    };
    uint8_t kinds[capacity];
    uint8_t classes[capacity];
    uint64_t values[capacity];
    uint8_t local_classes[2] = { BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID };
    uint8_t local_flags[2] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    uint32_t local_sizes[2] = { BUSTER_TIER0_MAX_COPY_BYTES, BUSTER_TIER0_MAX_COPY_BYTES };
    uint8_t local_alignments[2] = { 4, 4 };
    static const uint32_t sizes[] = { 1, 3, 8, 15, 16, 40, 256 };
    Arena arena = arena_reserve(1ull << 28);
    for (uint32_t integers = 0; integers <= 13; integers += 1)
    {
        for (uint32_t floatings = 0; floatings <= 16; floatings += 1)
        {
            for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
            {
                // The values, the copy between the objects, then the values discarded, the last pushed first.
                uint32_t count = 0;
                for (uint32_t k = 0; k < integers + floatings; k += 1)
                {
                    kinds[count] = BUSTER_TYPED_CONST;
                    classes[count] = k < integers ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64;
                    values[count] = k + 1;
                    count += 1;
                }
                kinds[count] = BUSTER_TYPED_LOCAL_ADDRESS;
                classes[count] = BUSTER_TYPED_CLASS_U64;
                values[count] = 0;
                kinds[count + 1] = BUSTER_TYPED_LOCAL_ADDRESS;
                classes[count + 1] = BUSTER_TYPED_CLASS_U64;
                values[count + 1] = 1;
                kinds[count + 2] = BUSTER_TYPED_COPY;
                classes[count + 2] = BUSTER_TYPED_CLASS_VOID;
                values[count + 2] = sizes[s];
                const uint32_t copy = count + 2;
                count += 3;
                for (uint32_t k = integers + floatings; k > 0; k -= 1)
                {
                    kinds[count] = BUSTER_TYPED_DISCARD;
                    classes[count] = k - 1 < integers ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64;
                    values[count] = 0;
                    count += 1;
                }
                kinds[count] = BUSTER_TYPED_RETURN;
                classes[count] = BUSTER_TYPED_CLASS_VOID;
                values[count] = 0;
                count += 1;
                Tier0Function function;
                memset(&function, 0, sizeof(function));
                function.kinds = kinds;
                function.classes = classes;
                function.values = values;
                function.count = count;
                function.local_classes = local_classes;
                function.local_flags = local_flags;
                function.local_sizes = local_sizes;
                function.local_alignments = local_alignments;
                function.local_count = 2;
                const uint64_t mark = arena.length;
                // The selections, from the scalar fusion's and depths' columns, which both paths compute alike.
                Tier0Entries entries;
                uint64_t* labels = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count / 64 + 2);
                bool ready = labels != 0 && tier0_push_entries(&entries, &arena, count);
                Tier0Frame frames[2];
                Tier0Error errors[2] = { TIER0_OK, TIER0_OK };
                memset(frames, 0, sizeof(frames));
                if (ready)
                {
                    memset(labels, 0, (count / 64 + 2) * sizeof(uint64_t));
                    tier0_labels(&function, labels);
                    tier0_fuse_scalar(&function, labels, &entries);
                    tier0_depths_scalar(&function, &entries);
                    (void)tier0_select(&function, &entries, &frames[0], &errors[0]);
                    (void)tier0_select_vector(&function, &entries, &frames[1], &errors[1]);
                }
                const bool wide = sizes[s] >= 16;
                const uint32_t depth = ready ? entries.depths[copy] : 0;
                const uint32_t floating = ready ? entries.floating_depths[copy] : 0;
                const bool too_deep = wide ? floating >= BUSTER_TIER0_FLOATING_DEPTHS : depth >= BUSTER_TIER0_INTEGER_DEPTHS;
                const uint32_t scratch = too_deep ? 0 : wide ? 1u << BUSTER_TIER0_XMM(floating) : 1u << tier0_integer_depths[depth];
                bool counted = ready;
                for (uint32_t path = 0; path < 2 && ready; path += 1)
                {
                    counted &= too_deep ? errors[path] == TIER0_ERROR_DEPTH : errors[path] == TIER0_OK && (frames[path].clobbers & scratch) != 0;
                }
                const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
                const Tier0Code vector = tier0_compile(&function, &arena);
                const bool compiled = too_deep ? scalar.error == TIER0_ERROR_DEPTH && vector.error == TIER0_ERROR_DEPTH
                                               : scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector);
                tier0_test_expect(tally, counted && compiled, "copies", "a copy of %u bytes over %u integers and %u floating values: its scratch %s", sizes[s], integers,
                                  floatings, counted ? "counted, but the paths differ" : "not in a selection's clobbers");
                arena_pop_to(&arena, mark);
            }
        }
    }
    arena_release(&arena);
}

// ---------------------------------------------------------------------------------------------------------------------
// Indirect variadic calls, run natively

// What a probe that a call reaches through r11 sees: the general argument registers, rax (whose al a variadic call sets
// to its vector registers' count), r11 and the vector argument registers' low halves; and what the caller returned.
typedef struct Tier0TestProbe Tier0TestProbe;
struct Tier0TestProbe
{
    uint64_t general[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint64_t rax;
    uint64_t r11;
    uint64_t vectors[BUSTER_ABI_VECTOR_ARGUMENTS];
    uint64_t result;
};

#define BUSTER_TIER0_TEST_PROBE_RESULT 0x5a17c0deu
#define BUSTER_TIER0_TEST_PROBE_RUNS 4

// The probe's records: every register it watches stored into the state at `state` (movabs r10, then a store each),
// then eax set to BUSTER_TIER0_TEST_PROBE_RESULT and ret. Returns the count.
static uint32_t tier0_test_probe(X86Instruction* records, uint64_t state)
{
    static const uint8_t general[BUSTER_ABI_INTEGER_ARGUMENTS] = {
        X86_REGISTER_RDI, X86_REGISTER_RSI, X86_REGISTER_RDX, X86_REGISTER_RCX, X86_REGISTER_R8, X86_REGISTER_R9,
    };
    uint32_t count = 0;
    records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(state >> 32), (int32_t)(uint32_t)state);
    count += 1;
    for (uint32_t g = 0; g < BUSTER_ABI_INTEGER_ARGUMENTS; g += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_RM64_R64, general[g], X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(sizeof(uint64_t) * g), 0);
        count += 1;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestProbe, rax), 0);
    records[count + 1] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_R11, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestProbe, r11), 0);
    count += 2;
    for (uint32_t v = 0; v < BUSTER_ABI_VECTOR_ARGUMENTS; v += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOVSD_XM64_X, (uint8_t)v, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE,
                                           (int32_t)(offsetof(Tier0TestProbe, vectors) + sizeof(uint64_t) * v), 0);
        count += 1;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_R32_IMM32, 0, X86_REGISTER_RAX, 0, 0, (int32_t)BUSTER_TIER0_TEST_PROBE_RESULT);
    records[count + 1] = tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0);
    return count + 2;
}

// A variadic call through a pointer (#363's review): a caller whose argument is the probe's address calls it with six
// integers and three doubles, past its named first one, so that al must be 3, as the probe sees it after the parallel
// moves have put the callee in r11. In the first fixture the callee is at depth 0, rax itself, under arguments whose
// moves are 2-cycles (rcx and rdi, rdx and rsi); in the second it is at depth 4, rdi, under four integers and two
// doubles saved around the call, so that its arguments' moves run through r11 (rbx, r8, rdi, r11 and rcx) and through
// the vector registers (xmm4, xmm2 and xmm0), and the values below must come back: the caller returns
// d0 - (d1 - (d2 - (d3 - (long)(f0 - (f1 - (double)call))))), in postorder, so that each entry's last operand is the
// entry before it. Both paths must compile each alike; the records encoded by each encoder run in a child, under an
// alarm.
static void tier0_test_variadic_calls(Tier0TestTally* tally, const Tier0Tables* tables)
{
    enum
    {
        capacity = 64,
    };
    static const uint64_t integers[BUSTER_ABI_INTEGER_ARGUMENTS] = {
        0x1111111111111111ull, 0x2222222222222222ull, 0x3333333333333333ull, 0x4444444444444444ull, 0x5555555555555555ull, 0x6666666666666666ull,
    };
    static const double doubles[3] = { 3.25, 4.75, -5.5 };
    static const uint64_t below_integers[4] = { 0x1000, 0x300, 0x50, 0x7 };
    static const double below_doubles[2] = { 1.5, 4.5 };
    // The caller's signature (the probe's address) and the call's (named: the first integer).
    const TypeId u64 = tier0_program_class_type(BUSTER_TYPED_CLASS_U64);
    const TypeId i64 = tier0_program_class_type(BUSTER_TYPED_CLASS_I64);
    const TypeId f64 = tier0_program_class_type(BUSTER_TYPED_CLASS_F64);
    const TypeId results[2] = { i64, i64 };
    const uint32_t counts[2] = { 1, 9 };
    const uint32_t named[2] = { 1, 1 };
    const uint8_t variadic[2] = { 0, 1 };
    const TypeId types[10] = { u64, u64, u64, u64, u64, u64, u64, f64, f64, f64 };
    const AbiSignatures signatures = {
        .results = results,
        .counts = counts,
        .named = named,
        .variadic = variadic,
        .count = 2,
        .arguments = types,
    };
    AbiLocation locations[10];
    AbiCall calls[2];
    abi_assign(&tables->abi, signatures, locations, calls);
    tier0_test_expect(tally, calls[1].vector_registers == 3 && calls[1].integer_registers == 6, "variadic calls", "the call's registers: %u general, %u vector",
                      calls[1].integer_registers, calls[1].vector_registers);
    Tier0TestProbe* shared = mmap(0, sizeof(Tier0TestProbe) * (BUSTER_TIER0_TEST_PROBE_RUNS + 1), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1ull << 28);
    uint8_t* images[BUSTER_TIER0_TEST_PROBE_RUNS] = { 0 };
    uint32_t lengths[BUSTER_TIER0_TEST_PROBE_RUNS] = { 0 };
    uint64_t expected[BUSTER_TIER0_TEST_PROBE_RUNS] = { 0 };
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED, "variadic calls", "no shared memory");
    for (uint32_t fixture = 0; fixture < 2 && ready; fixture += 1)
    {
        const uint32_t below = fixture == 0 ? 0 : 4;
        const uint32_t floating_below = fixture == 0 ? 0 : 2;
        uint8_t kinds[capacity];
        uint8_t classes[capacity];
        uint64_t values[capacity];
        uint32_t count = 0;
        for (uint32_t k = 0; k < below; k += 1)
        {
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_I64;
            values[count] = below_integers[k];
            count += 1;
        }
        for (uint32_t k = 0; k < floating_below; k += 1)
        {
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_F64;
            memcpy(&values[count], &below_doubles[k], sizeof(values[count]));
            count += 1;
        }
        kinds[count] = BUSTER_TYPED_LOCAL;
        classes[count] = BUSTER_TYPED_CLASS_U64;
        values[count] = 0;
        count += 1;
        for (uint32_t a = 0; a < BUSTER_ABI_INTEGER_ARGUMENTS; a += 1)
        {
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_U64;
            values[count] = integers[a];
            count += 1;
        }
        for (uint32_t a = 0; a < 3; a += 1)
        {
            kinds[count] = BUSTER_TYPED_CONST;
            classes[count] = BUSTER_TYPED_CLASS_F64;
            memcpy(&values[count], &doubles[a], sizeof(values[count]));
            count += 1;
        }
        kinds[count] = BUSTER_TYPED_CALL;
        classes[count] = BUSTER_TYPED_CLASS_I64;
        values[count] = 0;
        count += 1;
        if (fixture != 0)
        {
            // The result as a double, from which the doubles below subtract in turn, then as an integer, from which the
            // integers below do: none of it commutes, so values restored to each other's places show.
            static const uint8_t tail_kinds[8] = {
                BUSTER_TYPED_CONVERT, BUSTER_TYPED_SUB, BUSTER_TYPED_SUB, BUSTER_TYPED_CONVERT,
                BUSTER_TYPED_SUB,     BUSTER_TYPED_SUB, BUSTER_TYPED_SUB, BUSTER_TYPED_SUB,
            };
            static const uint8_t tail_classes[8] = {
                BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_I64,
                BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64,
            };
            memcpy(kinds + count, tail_kinds, sizeof(tail_kinds));
            memcpy(classes + count, tail_classes, sizeof(tail_classes));
            memset(values + count, 0, sizeof(tail_kinds) * sizeof(values[0]));
            count += 8;
        }
        kinds[count] = BUSTER_TYPED_RETURN;
        classes[count] = BUSTER_TYPED_CLASS_I64;
        values[count] = 0;
        count += 1;
        uint64_t result = BUSTER_TIER0_TEST_PROBE_RESULT;
        if (fixture != 0)
        {
            const double difference = below_doubles[0] - (below_doubles[1] - (double)(int64_t)result);
            const uint64_t truncated = (uint64_t)(int64_t)difference;
            result = below_integers[0] - (below_integers[1] - (below_integers[2] - (below_integers[3] - truncated)));
        }
        static const uint8_t local_classes[1] = { BUSTER_TYPED_CLASS_U64 };
        static const uint8_t local_flags[1] = { 0 };
        static const SymbolId targets[1] = { 0 };
        static const uint32_t call_first[1] = { 0 };
        static const uint32_t call_counts[1] = { 9 };
        static const uint8_t call_variadic[1] = { 1 };
        static const uint8_t argument_classes[9] = {
            BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
            BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F64,
        };
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = 1;
        function.parameter_count = 1;
        function.parameters = locations;
        function.result = calls[0].result;
        function.call_targets = targets;
        function.call_first = call_first;
        function.call_argument_counts = call_counts;
        function.call_variadic = call_variadic;
        function.calls = calls + 1;
        function.argument_locations = locations + 1;
        function.argument_classes = argument_classes;
        function.call_count = 1;
        function.argument_count = 9;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        ready = tier0_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "variadic calls",
                                  "fixture %u: rule \"%s\" at %u, error %d, or the paths differ", fixture, check.rule != 0 ? check.rule : "", check.node, scalar.error);
        ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.branch_count == 0 && scalar.reference_count == 0,
                                           "variadic calls", "fixture %u: an invalid record, a branch or a reference", fixture);
        // Each path's records by an encoder of its own: the vector path's by x86_encode, the scalar's by its twin.
        for (uint32_t encoder = 0; encoder < 2 && ready; encoder += 1)
        {
            const uint32_t run = fixture * 2 + encoder;
            const Tier0Code* code = encoder == 0 ? &vector : &scalar;
            const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(code->count);
            images[run] = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, room);
            const int64_t length = images[run] == 0 ? -1
                                   : encoder == 0   ? x86_encode(code->records, code->count, images[run], room, 0, 0)
                                                    : x86_encode_scalar(code->records, code->count, images[run], room, 0, 0);
            ready = tier0_test_expect(tally, length > 0, "variadic calls", "fixture %u: encoder %u failed", fixture, encoder);
            lengths[run] = ready ? (uint32_t)length : 0;
            expected[run] = result;
        }
        ready = ready && tier0_test_expect(tally, lengths[fixture * 2] == lengths[fixture * 2 + 1] && memcmp(images[fixture * 2], images[fixture * 2 + 1], lengths[fixture * 2]) == 0,
                                           "variadic calls", "fixture %u: the encoders differ", fixture);
    }
    // The callers at page starts and the probe on a page after them, mapped before the fork, so that the probe's
    // address, which r11 must hold when the probe runs, is known here.
    const uint64_t pages = (BUSTER_TIER0_TEST_PROBE_RUNS + 1) * BUSTER_TIER0_TEST_PAGE;
    uint8_t* region = ready ? mmap(0, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    uint8_t* probe_code = region != MAP_FAILED ? region + BUSTER_TIER0_TEST_PROBE_RUNS * BUSTER_TIER0_TEST_PAGE : 0;
    X86Instruction probe[32];
    const uint32_t probe_count = ready ? tier0_test_probe(probe, (uint64_t)(uintptr_t)shared) : 0;
    bool loaded = region != MAP_FAILED && x86_validate(probe, probe_count) == probe_count &&
                  x86_encode_scalar(probe, probe_count, probe_code, BUSTER_TIER0_TEST_PAGE, 0, 0) > 0;
    for (uint32_t run = 0; run < BUSTER_TIER0_TEST_PROBE_RUNS && loaded; run += 1)
    {
        memcpy(region + run * BUSTER_TIER0_TEST_PAGE, images[run], lengths[run]);
    }
    loaded = loaded && mprotect(region, pages, PROT_READ | PROT_EXEC) == 0;
    ready = ready && tier0_test_expect(tally, loaded, "variadic calls", "the code can't be loaded");
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t run = 0; run < BUSTER_TIER0_TEST_PROBE_RUNS; run += 1)
        {
            typedef uint64_t (*Tier0TestCaller)(uint64_t);
            Tier0TestCaller caller = 0;
            void* address = region + run * BUSTER_TIER0_TEST_PAGE;
            memcpy(&caller, &address, sizeof(caller));
            memset(&shared[0], 0xa5, sizeof(shared[0]));
            const uint64_t returned = caller((uint64_t)(uintptr_t)probe_code);
            shared[1 + run] = shared[0];
            shared[1 + run].result = returned;
        }
        _exit(0);
    }
    if (child > 0)
    {
        int status = 0;
        waitpid(child, &status, 0);
        const bool ran = tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "variadic calls", "the child's status %d", status);
        for (uint32_t run = 0; run < BUSTER_TIER0_TEST_PROBE_RUNS && ran; run += 1)
        {
            const Tier0TestProbe* seen = &shared[1 + run];
            bool arguments = (seen->rax & 0xff) == 3 && seen->r11 == (uint64_t)(uintptr_t)probe_code;
            for (uint32_t g = 0; g < BUSTER_ABI_INTEGER_ARGUMENTS; g += 1)
            {
                arguments &= seen->general[g] == integers[g];
            }
            for (uint32_t v = 0; v < 3; v += 1)
            {
                uint64_t bits = 0;
                memcpy(&bits, &doubles[v], sizeof(bits));
                arguments &= seen->vectors[v] == bits;
            }
            tier0_test_expect(tally, arguments, "variadic calls", "fixture %u encoder %u: the probe saw al %u, r11 %016" PRIx64 " and other arguments than the call's",
                              run / 2, run % 2, (uint32_t)(seen->rax & 0xff), seen->r11);
            tier0_test_expect(tally, seen->result == expected[run], "variadic calls", "fixture %u encoder %u: %016" PRIx64 ", not %016" PRIx64, run / 2, run % 2,
                              seen->result, expected[run]);
        }
    }
    if (region != MAP_FAILED)
    {
        munmap(region, pages);
    }
    arena_release(&arena);
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestProbe) * (BUSTER_TIER0_TEST_PROBE_RUNS + 1));
    }
}

static int run_tier0_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint32_t count = 300;
    uint32_t cross_count = 60;
    bool cross = false;
    bool keep = false;
    const char* clang = "clang";
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
        else if (strcmp(argv[a], "--cross-count") == 0 && a + 1 < argc)
        {
            cross_count = (uint32_t)strtoul(argv[a + 1], 0, 0);
            a += 1;
        }
        else if (strcmp(argv[a], "--clang") == 0 && a + 1 < argc)
        {
            clang = argv[a + 1];
            a += 1;
        }
        else if (strcmp(argv[a], "--cross-check") == 0)
        {
            cross = true;
        }
        else if (strcmp(argv[a], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: build/compiler test-tier0 [--seed N] [--count N] [--cross-check [--cross-count N] [--clang PATH] [--keep]]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-tier0: the tables can't be built\n");
    }
    else
    {
        Tier0Tables tables;
        Tier0TestTally total = { 0, 0 };
        if (tier0_tables_open(&tables))
        {
            static const Tier0ProgramOptions shapes[4] = {
                { .functions = 1, .statements = 6, .expression_nodes = 6, .calls = false, .loops = false, .floating = false, .pointers = false },
                { .functions = 3, .statements = 12, .expression_nodes = 10, .calls = true, .loops = true, .floating = false, .pointers = false, .indirect = true },
                { .functions = 4, .statements = 16, .expression_nodes = 14, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true },
                { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true },
            };
            static const char* const names[4] = { "programs: one function", "programs: integers and calls", "programs: everything", "programs: larger" };
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_moves(&tally);
                tier0_test_report("moves", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_applies(&tally);
                tier0_test_report("applies", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_errors(&tally);
                tier0_test_report("errors", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_objects(&tally, seed);
                tier0_test_report("objects", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_copies(&tally);
                tier0_test_report("copies", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_variadic_calls(&tally, &tables);
                tier0_test_report("variadic calls", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
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
            if (cross)
            {
                Tier0TestTally tally = { 0, 0 };
                Tier0ProgramOptions defined = shapes[3];
                defined.defined = true;
                tier0_test_cross(&tally, &tables, seed, cross_count, defined, clang, keep);
                tier0_test_report("cross-check: clang -O0", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            tier0_tables_close(&tables);
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
