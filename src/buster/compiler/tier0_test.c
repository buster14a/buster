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

#include <signal.h>
#include <unistd.h>
#include <asm/prctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// The calling convention's compiled worlds keep their types within Tier 0's objects' alignments, and their calls with
// memory-class values within its depths (calling_convention_test.c).
_Static_assert(BUSTER_ABI_TEST_TIER0_INTEGER_DEPTHS == BUSTER_TIER0_INTEGER_DEPTHS && BUSTER_ABI_TEST_TIER0_FLOATING_DEPTHS == BUSTER_TIER0_FLOATING_DEPTHS,
               "the compiled worlds' depths must be Tier 0's");
_Static_assert(BUSTER_ABI_TEST_TIER0_MAX_ALIGNMENT_LOG2 == BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2, "the compiled worlds' alignments must be Tier 0's");

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
// the native run hash the same bytes. The thread-locals (the `access` option's) have a place in the TLS block too, where
// the native run reaches them: each at tls_offsets[g] (8-aligned) in a block of tls_size bytes, a multiple of 64, which
// the thread pointer ends.
typedef struct Tier0TestData Tier0TestData;
struct Tier0TestData
{
    uint32_t offsets[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t size;
    uint32_t tls_offsets[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    uint32_t tls_size;
};

// Whether global g is a thread-local: reached by one of the TLS accesses.
static bool tier0_test_thread_local(const Tier0Program* program, uint32_t g)
{
    return program->global_access[g] == BUSTER_TIER0_ACCESS_LOCAL_EXEC || program->global_access[g] == BUSTER_TIER0_ACCESS_INITIAL_EXEC;
}

static Tier0TestData tier0_test_layout(const Tier0Program* program)
{
    Tier0TestData data;
    memset(&data, 0, sizeof(data));
    uint32_t offset = 0;
    uint32_t tls_offset = 0;
    for (uint32_t g = 0; g < program->global_count; g += 1)
    {
        const uint32_t bytes = (tier0_class_bytes(program->global_classes[g]) * program->global_elements[g] + 7) & ~7u;
        data.offsets[g] = offset;
        offset += bytes;
        data.tls_offsets[g] = tls_offset;
        tls_offset += tier0_test_thread_local(program, g) ? bytes : 0;
    }
    data.size = offset;
    data.tls_size = (tls_offset + 63) & ~63u;
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
    else if (to == BUSTER_TYPED_CLASS_U64 && from_floating)
    {
        // Tier 0's unsigned truncation (tier0_emit_u64_convert): the signed one below 2^63 (or unordered), and from 2^63
        // the signed one of the value less 2^63, its top bit flipped.
        if (from == BUSTER_TYPED_CLASS_F32)
        {
            float f = 0.0f;
            const uint32_t low = (uint32_t)value;
            memcpy(&f, &low, sizeof(f));
            const bool high = f >= 0x1p63f;
            result = (uint64_t)_mm_cvttss_si64(_mm_set_ss(high ? f - 0x1p63f : f)) ^ (high ? 1ull << 63 : 0);
        }
        else
        {
            const double d = tier0_test_double(from, value);
            const bool high = d >= 0x1p63;
            result = (uint64_t)_mm_cvttsd_si64(_mm_set_sd(high ? d - 0x1p63 : d)) ^ (high ? 1ull << 63 : 0);
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

// A frame of the interpreter: the function, where it is, its locals' slots, its objects' memory, its stack's base, and
// how much of the allocations' area was taken when it began, which its return gives back; and for its return, whether
// its caller's call pushes a value, and where a record it returns in registers goes (its caller's result object,
// `result_size` bytes).
typedef struct Tier0TestFrame Tier0TestFrame;
struct Tier0TestFrame
{
    uint32_t function;
    uint32_t entry;
    uint64_t* locals;
    uint8_t* objects;
    uint32_t stack_base;
    uint32_t allocated;
    bool push;
    uint8_t* result_object;
    uint32_t result_size;
};

#define BUSTER_TIER0_TEST_FRAMES 256
#define BUSTER_TIER0_TEST_STACK 4096
#define BUSTER_TIER0_TEST_SLOTS (BUSTER_TIER0_TEST_FRAMES * BUSTER_TIER0_GENERATE_MAX_LOCALS)
// A frame's objects' bytes, each object at a multiple of its alignment and of 16, the frame's at a multiple of 4096, the
// most the generator aligns one to (its objects take a few hundred bytes at most but those aligned to 4096, a few
// thousand, and its records' objects up to 136 each).
#define BUSTER_TIER0_TEST_OBJECT_BYTES 65536
#define BUSTER_TIER0_TEST_OBJECT_ALIGNMENT 4096
// A function's address in the interpreter: its index under this tag.
#define BUSTER_TIER0_TEST_FUNCTION_TAG 0xf0f0000000000000ull
#define BUSTER_TIER0_TEST_STEPS 200000000ull
// The area ALLOCA takes from, every allocation a multiple of 16 at a multiple of 16, as the native stack has them.
#define BUSTER_TIER0_TEST_ALLOCATION_BYTES (1u << 20)

// Runs the program's entry (its last function) on three arguments over the globals at `globals` (the data layout's),
// and returns its result; `fault` is set on a division the generator should have made safe, past the step budget, on
// an indirect call that reaches a function of another type, on allocations past their area, or on a restore to a
// position the frame's allocations never had.
static uint64_t tier0_test_interpret(const Tier0Program* program, const Tier0Functions* functions, const Tier0TestData* data, uint8_t* globals,
                                     const uint64_t* inputs, bool* fault)
{
    static Tier0TestFrame frames[BUSTER_TIER0_TEST_FRAMES];
    static uint64_t stack[BUSTER_TIER0_TEST_STACK];
    static uint64_t slots[BUSTER_TIER0_TEST_SLOTS];
    static uint8_t objects[BUSTER_TIER0_TEST_FRAMES * BUSTER_TIER0_TEST_OBJECT_BYTES] __attribute__((aligned(BUSTER_TIER0_TEST_OBJECT_ALIGNMENT)));
    static uint8_t allocations[BUSTER_TIER0_TEST_ALLOCATION_BYTES] __attribute__((aligned(64)));
    uint32_t allocated = 0;
    // Each object's offset in its frame's memory, by the program's local index.
    static uint32_t object_offsets[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS * BUSTER_TIER0_GENERATE_MAX_LOCALS];
    uint32_t depth = 1;
    uint32_t top = 0;
    uint64_t result = 0;
    uint64_t steps = 0;
    const uint32_t entry = program->function_count - 1;
    *fault = false;
    // The most bytes a frame's objects take, which a call clears.
    uint32_t extent = 0;
    for (uint32_t f = 0; f < program->function_count; f += 1)
    {
        const Tier0GeneratedFunction* function = &program->functions[f];
        uint32_t offset = 0;
        for (uint32_t l = 0; l < function->local_count; l += 1)
        {
            const uint32_t size = program->local_sizes != 0 ? program->local_sizes[function->first_local + l] : 0;
            const uint32_t alignment = size != 0 ? 1u << program->local_alignments[function->first_local + l] : 16;
            offset = (offset + (alignment > 16 ? alignment : 16) - 1) & ~((alignment > 16 ? alignment : 16) - 1);
            object_offsets[function->first_local + l] = offset;
            offset += (size + 15) & ~15u;
        }
        *fault |= offset > BUSTER_TIER0_TEST_OBJECT_BYTES;
        extent = offset > extent ? offset : extent;
    }
    extent = extent < BUSTER_TIER0_TEST_OBJECT_BYTES ? extent : BUSTER_TIER0_TEST_OBJECT_BYTES;
    frames[0] = (Tier0TestFrame){ entry, 0, slots, objects, 0, 0, false, 0, 0 };
    memset(slots, 0, sizeof(uint64_t) * BUSTER_TIER0_GENERATE_MAX_LOCALS);
    memset(objects, 0, extent);
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
            allocated = frame->allocated;
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
            memset(callee_objects, 0, extent);
            top -= arguments;
            const uint32_t base = top;
            if (f->call_targets[value] == 0)
            {
                // An indirect call's callee, under its arguments: a function's tagged index, the one the pointer holds
                // when the call runs. Its type must be the call's, checked here apart from the generator's choice of
                // candidates: its result's class and record the call's callee's, and each parameter's class and record
                // its argument's (a record's argument is its address, U64).
                top -= 1;
                const uint64_t address = stack[top];
                const uint32_t held = (uint32_t)(address & 0xffff);
                const bool function_held = (address & ~0xffffull) == BUSTER_TIER0_TEST_FUNCTION_TAG && held < program->function_count;
                const Tier0GeneratedFunction* target = &program->functions[function_held ? held : callee];
                const uint32_t spelled = target->hidden ? 1 : 0;
                bool typed = function_held && target->parameter_count == arguments + spelled && target->result_class == class &&
                             target->result_record == program->functions[callee].result_record;
                for (uint32_t a = 0; a < arguments && typed; a += 1)
                {
                    const uint32_t at = target->first_local + spelled + a;
                    const uint32_t record = program->local_records != 0 ? program->local_records[at] : BUSTER_TIER0_GENERATE_NO_RECORD;
                    const uint32_t argument = f->call_first[value] + a;
                    typed = record != BUSTER_TIER0_GENERATE_NO_RECORD ? program->argument_records[argument] == record
                                                                      : program->local_classes[at] == f->argument_classes[argument];
                }
                *fault |= !typed;
                callee = function_held ? held : callee;
            }
            // The arguments into the callee's parameters: a value into its local, a record's bytes from its address
            // into the parameter's object; a hidden pointer, parameter 0, is the call's result object's address.
            const Tier0GeneratedFunction* target = &program->functions[callee];
            const uint32_t spelled = target->hidden ? 1 : 0;
            const uint32_t object = f->call_result_objects != 0 ? f->call_result_objects[value] : 0;
            uint8_t* result_object = object != 0 ? frame->objects + object_offsets[program->functions[frame->function].first_local + object - 1] : 0;
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                const uint32_t size = f->argument_sizes != 0 ? f->argument_sizes[f->call_first[value] + a] : 0;
                const uint32_t at = target->first_local + spelled + a;
                if (size != 0)
                {
                    memcpy(callee_objects + object_offsets[at], (const void*)(uintptr_t)stack[base + a], size);
                }
                else
                {
                    locals[spelled + a] = stack[base + a];
                }
            }
            if (target->hidden)
            {
                locals[0] = (uint64_t)(uintptr_t)result_object;
            }
            const Tier0Function* callee_function = &functions->functions[callee];
            frames[depth] = (Tier0TestFrame){ callee, 0, locals, callee_objects, top, allocated, class != BUSTER_TYPED_CLASS_VOID, result_object,
                                              callee_function->result_size };
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
        case BUSTER_TYPED_ALLOCA:
        {
            // The bytes rounded up to 16, past what is taken; a count past the area is a fault.
            const uint64_t bytes = stack[top - 1] <= BUSTER_TIER0_TEST_ALLOCATION_BYTES ? (stack[top - 1] + 15) & ~15ull : UINT64_MAX;
            *fault |= bytes > BUSTER_TIER0_TEST_ALLOCATION_BYTES - allocated;
            stack[top - 1] = (uint64_t)(uintptr_t)(allocations + (*fault ? 0 : allocated));
            allocated += *fault ? 0 : (uint32_t)bytes;
            break;
        }
        case BUSTER_TYPED_STACK_SAVE:
            stack[top++] = allocated;
            break;
        case BUSTER_TYPED_STACK_RESTORE:
            // A position this frame's allocations passed, and no further than they are.
            top -= 1;
            *fault |= stack[top] < frame->allocated || stack[top] > allocated;
            allocated = *fault ? allocated : (uint32_t)stack[top];
            break;
        case BUSTER_TYPED_RETURN:
        {
            // A record returned in registers is its bytes' address (result_size), copied into the caller's object.
            const uint64_t returned = class != BUSTER_TYPED_CLASS_VOID ? stack[top - 1] : 0;
            if (frame->result_size != 0 && frame->result_object != 0)
            {
                memcpy(frame->result_object, (const void*)(uintptr_t)returned, frame->result_size);
            }
            depth -= 1;
            top = frame->stack_base;
            allocated = frame->allocated;
            if (depth != 0 && frame->push && class != BUSTER_TYPED_CLASS_VOID)
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
// its instruction's end, its target symbol and its kind (BUSTER_LINK_FIXUP_*).
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
    uint8_t kinds[BUSTER_TIER0_TEST_FIXUPS];
    uint32_t fixup_count;
    uint64_t records;
    uint64_t entries;
    uint64_t deferred;
    uint64_t deep;
    // The functions whose frames are realigned (Tier0Code.frame_alignment_log2), and those among them with a base.
    uint64_t realigned;
    uint64_t based;
};

// Whether two compilations of a function made the same code: records, branch columns, references, labels' records and
// the entries' first records.
static bool tier0_test_same_code(const Tier0Code* a, const Tier0Code* b)
{
    bool same = a->error == b->error && a->count == b->count && a->branch_count == b->branch_count && a->reference_count == b->reference_count &&
                a->deferred == b->deferred && a->label_count == b->label_count && a->deep == b->deep && a->entry_count == b->entry_count &&
                (a->entry_records == 0) == (b->entry_records == 0) && a->frame_saved == b->frame_saved && a->frame_size == b->frame_size &&
                a->frame_pointer == b->frame_pointer && a->frame_alignment_log2 == b->frame_alignment_log2 && a->frame_base == b->frame_base;
    same = same && (a->error != TIER0_OK || a->entry_records == 0 || memcmp(a->entry_records, b->entry_records, ((uint64_t)a->entry_count + 1) * sizeof(uint32_t)) == 0);
    same = same && memcmp(a->records, b->records, a->count * sizeof(X86Instruction)) == 0;
    same = same && memcmp(a->branches, b->branches, a->branch_count * sizeof(uint32_t)) == 0;
    same = same && memcmp(a->targets, b->targets, a->branch_count * sizeof(uint32_t)) == 0;
    same = same && (a->label_count == 0 || memcmp(a->label_records, b->label_records, a->label_count * sizeof(uint32_t)) == 0);
    for (uint32_t r = 0; same && r < a->reference_count; r += 1)
    {
        const LinkReference* x = &a->references[r];
        const LinkReference* y = &b->references[r];
        same = x->instruction == y->instruction && x->field == y->field && x->kind == y->kind && x->target == y->target && x->addend == y->addend;
    }
    return same;
}

// A prefix value remains live below the call's arguments. The byte effects wrap at the signed boundary,
// but their true post-call depths are two (prefix plus result), or an explicit depth refusal at240.
static void tier0_test_wide_call_depths(Tier0TestTally* tally)
{
    static const uint32_t counts[] = { 127, 128, 129, 130, 237, 238 };
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    const bool ready = tier0_test_expect(tally, scratch.base != NULL, "wide call depths", "no selector scratch");
    for (uint32_t floating = 0; ready && floating < 2; floating += 1)
    {
        for (uint32_t indirect = 0; indirect < 2; indirect += 1)
        {
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
            {
                arena_pop_to(&scratch, 0);
                const uint32_t arguments = counts[c];
                const uint8_t kind = BUSTER_TYPED_CALL;
                const uint8_t class = floating != 0 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_U64;
                const uint64_t value = 0;
                const SymbolId target = indirect != 0 ? 0 : 1;
                uint8_t absorbed = 0;
                uint16_t template = 0;
                const uint32_t before_integer = floating != 0 ? indirect : arguments + 1 + indirect;
                const uint32_t before_floating = floating != 0 ? arguments + 1 : 0;
                int8_t integer_effect = (int8_t)(floating != 0 ? -(int32_t)indirect : 1 - (int32_t)arguments - (int32_t)indirect);
                int8_t floating_effect = (int8_t)(floating != 0 ? 1 - (int32_t)arguments : 0);
                uint8_t integer_depth = (uint8_t)before_integer;
                uint8_t floating_depth = (uint8_t)before_floating;
                Tier0Entries entries =
                {
                    .absorbed = &absorbed,
                    .templates = &template,
                    .integer_effects = &integer_effect,
                    .floating_effects = &floating_effect,
                    .depths = &integer_depth,
                    .floating_depths = &floating_depth,
                };
                const Tier0Function function =
                {
                    .count = 1,
                    .kinds = &kind,
                    .classes = &class,
                    .values = &value,
                    .call_count = 1,
                    .call_targets = &target,
                    .call_argument_counts = &arguments,
                };
                Tier0Frame frame = { .depth_count = 15 };
                uint32_t deep_count = 0;
                const Tier0Error error = tier0_select_deep(&function, &entries, &frame, &scratch, false, &deep_count);
                const bool refused = before_integer >= BUSTER_TIER0_MAX_DEPTH;
                bool correct = refused ? error == TIER0_ERROR_DEPTH : error == TIER0_OK;
                if (correct && !refused)
                {
                    const Tier0DeepEntry deep = tier0_deep_entry(&function, &entries, 0);
                    const uint32_t flags = entries.deep[0];
                    correct = deep_count == 1 && deep.before[0] == before_integer && deep.before[1] == before_floating &&
                              deep.exit[0] == (floating != 0 ? 0u : 2u) && deep.exit[1] == (floating != 0 ? 2u : 0u) &&
                              (flags & (BUSTER_TIER0_DEEP_CALL | BUSTER_TIER0_DEEP_FILL_INTEGER | BUSTER_TIER0_DEEP_FILL_FLOATING)) ==
                              (BUSTER_TIER0_DEEP_CALL | BUSTER_TIER0_DEEP_FILL_INTEGER | BUSTER_TIER0_DEEP_FILL_FLOATING);
                }
                tier0_test_expect(tally, correct, "wide call depths", "%u arguments, %s result, %s target: modulo effects preserve the prefix and result", arguments,
                                  floating != 0 ? "floating" : "integer", indirect != 0 ? "indirect" : "direct");
            }
        }
    }
    arena_release(&scratch);
}

// Scalar per-entry lengths must hold a whole call's copies, not only one argument's. Exercise the 16-bit
// crossing in ordinary and deep calls without executing their source addresses; both encoders must agree too.
static void tier0_test_large_call_records(Tier0TestTally* tally)
{
    enum { ARGUMENTS = 16, ENTRIES = ARGUMENTS + 2 };
    uint8_t kinds[ENTRIES] = { 0 };
    uint8_t classes[ENTRIES] = { 0 };
    uint64_t values[ENTRIES] = { 0 };
    uint8_t argument_classes[ARGUMENTS];
    uint32_t sizes[ARGUMENTS];
    AbiLocation locations[ARGUMENTS];
    const uint32_t first = 0;
    const SymbolId target = 1;
    const uint8_t variadic = 0;
    static const uint32_t counts[] = { 7, 8, 16 };
    Arena arena = arena_reserve(UINT64_C(1) << 27);
    const bool ready = tier0_test_expect(tally, arena.base != NULL, "large call records", "no output arena");
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        arena_pop_to(&arena, 0);
        const uint32_t arguments = counts[c];
        for (uint32_t a = 0; a < arguments; a += 1)
        {
            kinds[a] = BUSTER_TYPED_CONST;
            classes[a] = BUSTER_TYPED_CLASS_U64;
            values[a] = 0x1000 + a * 8;
            argument_classes[a] = BUSTER_TYPED_CLASS_U64;
            sizes[a] = BUSTER_TIER0_MAX_ARGUMENT_BYTES;
            locations[a] = (AbiLocation)
            {
                .kind = BUSTER_ABI_LOCATION_STACK,
                .stack_offset = a * BUSTER_TIER0_MAX_ARGUMENT_BYTES,
                .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
            };
        }
        kinds[arguments] = BUSTER_TYPED_CALL;
        classes[arguments] = BUSTER_TYPED_CLASS_VOID;
        values[arguments] = 0;
        kinds[arguments + 1] = BUSTER_TYPED_RETURN;
        classes[arguments + 1] = BUSTER_TYPED_CLASS_VOID;
        values[arguments + 1] = 0;
        const AbiCall call =
        {
            .stack_bytes = arguments * BUSTER_TIER0_MAX_ARGUMENT_BYTES,
            .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
            .result = { .kind = BUSTER_ABI_RESULT_IGNORE },
        };
        const Tier0Function function =
        {
            .count = arguments + 2,
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .call_count = 1,
            .calls = &call,
            .call_targets = &target,
            .call_first = &first,
            .call_argument_counts = &arguments,
            .call_variadic = &variadic,
            .argument_count = arguments,
            .argument_classes = argument_classes,
            .argument_locations = locations,
            .argument_sizes = sizes,
            .symbol_count = 2,
            .result = { .kind = BUSTER_ABI_RESULT_IGNORE },
            .entry_records = true,
        };
        const Tier0Check checked = tier0_validate(&function);
        const Tier0Code scalar = checked.error == TIER0_OK ? tier0_compile_scalar(&function, &arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
        const Tier0Code vector = checked.error == TIER0_OK ? tier0_compile(&function, &arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
        bool same = scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector) &&
                    (arguments == 7 ? scalar.count < UINT16_MAX : scalar.count > UINT16_MAX) && (arguments != 16 || scalar.deep != 0) &&
                    x86_validate(scalar.records, scalar.count) == scalar.count;
        const uint64_t capacity = same ? BUSTER_X86_ENCODER_CAPACITY(scalar.count) : 0;
        uint8_t* a = same ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, capacity) : NULL;
        uint8_t* b = same ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, capacity) : NULL;
        uint32_t* ends = same ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, (uint64_t)scalar.count + 1) : NULL;
        same = same && a != NULL && b != NULL && ends != NULL;
        const int64_t a_length = same ? x86_encode(scalar.records, scalar.count, a, capacity, 0, ends) : -1;
        const int64_t b_length = same ? x86_encode_scalar(vector.records, vector.count, b, capacity, 0, ends) : -1;
        tier0_test_expect(tally, same && a_length >= 0 && a_length == b_length && memcmp(a, b, (uint64_t)a_length) == 0,
                          "large call records", "%u maximum-size aggregates: validation %u, scalar %u/%u records, vector %u/%u", arguments,
                          checked.error, scalar.error, scalar.count, vector.error, vector.count);
    }
    arena_release(&arena);
}

// Prefix totals and the epilogue must be checked before narrowed positions can allocate or publish output. The
// maximum representable boundary reaches the real arena-capacity check; an unrepresentable total touches nothing.
static void tier0_test_record_count_bounds(Tier0TestTally* tally)
{
    uint32_t lengths[] = { UINT32_MAX - 64, 32, 64 };
    uint32_t positions[4] = { 0 };
    Tier0Entries entries = { .lengths = lengths, .positions = positions };
    const Tier0Function function = { .count = BUSTER_ARRAY_LENGTH(lengths) };
    tier0_positions_scalar(&function, &entries, 7);
    const uint64_t expected = (uint64_t)UINT32_MAX + 39;
    tier0_test_expect(tally, entries.record_count == expected && positions[0] == 7 && positions[1] == UINT32_MAX - 57 && positions[2] == UINT32_MAX - 25,
                      "record count bounds", "the scalar prefix retains its full cumulative count");
    Arena arena = arena_reserve(4096);
    const bool ready = tier0_test_expect(tally, arena.base != NULL, "record count bounds", "no small output arena");
    if (ready)
    {
        const uint64_t limit = UINT32_MAX - BUSTER_TIER0_TEMPLATE_RECORDS;
        Tier0Code boundary = { 0 };
        const bool room = tier0_push_output(&boundary, &arena, limit, (Tier0Counts){ 0 }, NULL, 0);
        tier0_test_expect(tally, !room && boundary.count == limit, "record count bounds", "the exact representable boundary reaches real capacity checking");
        arena_pop_to(&arena, 0);
        const uint64_t counts[] = { limit + 1, entries.record_count, (uint64_t)UINT32_MAX - 3 + 4, UINT64_MAX };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            Tier0Code code = { .count = 19, .node = 27 };
            uint8_t before[sizeof(code)];
            memcpy(before, &code, sizeof(code));
            const uint64_t mark = arena.length;
            const bool accepted = tier0_push_output(&code, &arena, counts[c], (Tier0Counts){ 0 }, NULL, 0);
            tier0_test_expect(tally, !accepted && arena.length == mark && memcmp(before, &code, sizeof(code)) == 0,
                              "record count bounds", "unrepresentable total %" PRIu64 " is rejected before allocation or publication", counts[c]);
        }
    }
    arena_release(&arena);
}

// #221: every record of a function's code must have a covered form (x86_effects), which the effects' consumers (the
// unwind tables' analyzer, a scheduler) need of each record Tier 0 emits, but an X86 entry's: the front end's row, any
// VEX or EVEX form. A function with X86 entries needs its code's entries' first records (Tier0Function.entry_records)
// to tell those apart; without them, its records go unchecked.
static bool tier0_test_covered(Tier0TestTally* tally, const char* check, const char* name, const Tier0Function* function, const Tier0Code* code)
{
    bool rows = false;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        rows |= function->kinds[e] == BUSTER_TYPED_X86;
    }
    const bool entries = code->entry_records != 0 && code->entry_count == function->count;
    bool result = true;
    // The first entry whose first record is past record r: r is that entry's predecessor's (the prologue's before entry
    // 0's first record, the epilogue's from the last entry's end).
    uint32_t next = 0;
    for (uint32_t r = 0; result && (!rows || entries) && r < code->count; r += 1)
    {
        while (entries && next <= function->count && code->entry_records[next] <= r)
        {
            next += 1;
        }
        const bool row = entries && next >= 1 && next <= function->count && function->kinds[next - 1] == BUSTER_TYPED_X86;
        result = row || tier0_test_expect(tally, x86_effects(code->records[r].form)->operation != X86_OPERATION_NONE, check,
                                          "%s: record %u (%s) has no covered form, which the effects' consumers need", name, r,
                                          x86_semantics_test_name(code->records[r].form));
    }
    return result;
}

// Compiles every function of the program by both paths (which must agree), checks the records and branch columns,
// sizes the branches and encodes each function into `code`.
static void tier0_test_scan_program(Tier0TestTally* tally, const Tier0Function* function, Arena* arena, uint64_t seed, uint32_t changes, uint64_t* taken);

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
        // The validation's twins (#569): the scalar validator's verdict alike; the vector path taking the function itself;
        // and, for every fourth program, copies changed in one place, which it may take only where the scalar validator
        // does (tier0_validate_test.c).
        const Tier0Check twin = tier0_validate_scalar(function);
        result = result && tier0_test_expect(tally, twin.error == check.error && twin.node == check.node && twin.rule == check.rule, "validation",
                                             "seed %" PRIu64 " function %u: the scalar validator says %d at %u", seed, f, twin.error, twin.node);
        uint64_t taken[2] = { 0, 0 };
        if (result)
        {
            tier0_test_scan_program(tally, function, arena, seed * 64 + f, seed % 4 == 0 ? 4 : 0, taken);
        }
        // The entries' first records asked for (Tier0Function.entry_records); and a compile without them, which must
        // make the same code and none of them.
        Tier0Function wanted = *function;
        wanted.entry_records = true;
        Tier0Code scalar = result ? tier0_compile_scalar(&wanted, arena) : (Tier0Code){ 0 };
        Tier0Code vector = result ? tier0_compile(&wanted, arena) : (Tier0Code){ 0 };
        const Tier0Code plain = result ? tier0_compile(function, arena) : (Tier0Code){ 0 };
        result = result && tier0_test_expect(tally, scalar.error == TIER0_OK, "compile", "seed %" PRIu64 " function %u: error %d at entry %u", seed, f, scalar.error,
                                             scalar.node);
        result = result && tier0_test_expect(tally, tier0_test_same_code(&scalar, &vector), "twins", "seed %" PRIu64 " function %u: the paths differ", seed, f);
        result = result && tier0_test_expect(tally, plain.error == TIER0_OK && plain.entry_records == 0 && plain.entry_count == 0 && plain.count == vector.count &&
                                                    memcmp(plain.records, vector.records, (uint64_t)plain.count * sizeof(X86Instruction)) == 0,
                                             "entries", "seed %" PRIu64 " function %u: a compile without the entries' first records differs or makes them", seed, f);
        code->records += scalar.count;
        code->entries += function->count;
        code->deferred += scalar.deferred;
        code->deep += scalar.deep;
        code->realigned += scalar.frame_alignment_log2 != 0 ? 1 : 0;
        code->based += scalar.frame_base ? 1 : 0;
        if (result)
        {
            const uint64_t valid = x86_validate(scalar.records, scalar.count);
            result = tier0_test_expect(tally, valid == scalar.count, "records", "seed %" PRIu64 " function %u: record %" PRIu64 " of %u is invalid", seed, f, valid,
                                       scalar.count);
            char name[64];
            snprintf(name, sizeof(name), "seed %" PRIu64 " function %u", seed, f);
            result = result && tier0_test_covered(tally, "effects", name, function, &scalar);
        }
        if (result)
        {
            // The entries' first records: one per entry and the epilogue's, ascending, within the records.
            bool ascending = scalar.entry_count == function->count && scalar.entry_records[scalar.entry_count] <= scalar.count;
            for (uint32_t e = 0; ascending && e < scalar.entry_count; e += 1)
            {
                ascending = scalar.entry_records[e] <= scalar.entry_records[e + 1];
            }
            result = tier0_test_expect(tally, ascending, "entries", "seed %" PRIu64 " function %u: the entries' first records aren't ascending within the records", seed, f);
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
                    code->kinds[code->fixup_count] = reference->kind;
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
//
// The data pages hold the globals, then a GOT slot per SymbolId (a global's or a function's address, or a thread-local's
// offset from the thread pointer), then the thread-locals' block and the thread pointer at its end, whose first 8 bytes
// hold its own address, as the C library's thread control block does (what fs:[0] reads). Each run copies the
// thread-locals' first values into the block, points FS at the thread pointer for the entry's call alone (nothing but
// the program's code runs while it is the block's), and copies them back beside the other globals, whose bytes it
// hashes as the interpreter's.
static bool tier0_test_run(const Tier0Program* program, const Tier0TestCode* code, const Tier0TestData* data, uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3],
                           Tier0TestOutcome* outcomes, int* status)
{
    const uint64_t code_pages = ((uint64_t)code->size + BUSTER_TIER0_TEST_PAGE - 1) / BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_PAGE + BUSTER_TIER0_TEST_PAGE;
    const uint32_t symbols = program->global_count + program->function_count + 1;
    const uint64_t got_offset = ((uint64_t)data->size + 63) & ~63ull;
    const uint64_t tls_offset = (got_offset + 8ull * symbols + 63) & ~63ull;
    const uint64_t data_bytes = tls_offset + data->tls_size + 64;
    const uint64_t data_pages = (data_bytes + BUSTER_TIER0_TEST_PAGE - 1) / BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_PAGE + BUSTER_TIER0_TEST_PAGE;
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
        uint8_t* got = data_base + got_offset;
        uint8_t* tls = data_base + tls_offset;
        uint8_t* thread_pointer = tls + data->tls_size;
        const uint64_t self = (uint64_t)(uintptr_t)thread_pointer;
        memcpy(thread_pointer, &self, sizeof(self));
        // Each symbol's address, a thread-local's in the block, and its slot's value.
        for (SymbolId s = 1; s < symbols; s += 1)
        {
            const bool global = s <= program->global_count;
            const bool thread_local = global && tier0_test_thread_local(program, s - 1);
            const uint8_t* address = !global ? region + code->offsets[s - 1 - program->global_count] : thread_local ? tls + data->tls_offsets[s - 1] : data_base + data->offsets[s - 1];
            const uint64_t slot = thread_local ? (uint64_t)(address - thread_pointer) : (uint64_t)(uintptr_t)address;
            memcpy(got + 8ull * s, &slot, sizeof(slot));
        }
        memcpy(region, code->code, code->size);
        for (uint32_t x = 0; x < code->fixup_count; x += 1)
        {
            const SymbolId target = code->targets[x];
            const uint8_t kind = code->kinds[x];
            uint64_t slot = 0;
            memcpy(&slot, got + 8ull * target, sizeof(slot));
            const bool through_slot = kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
            const bool thread_local = target <= program->global_count && tier0_test_thread_local(program, target - 1);
            const uint8_t* address = through_slot ? got + 8ull * target
                                     : target <= program->global_count ? (thread_local ? tls + data->tls_offsets[target - 1] : data_base + data->offsets[target - 1])
                                                                       : region + code->offsets[target - program->global_count - 1];
            // A TPOFF32 is the thread-local's offset from the thread pointer (its slot's value); the others are
            // measured from the instruction's end.
            const int32_t value = kind == BUSTER_LINK_FIXUP_TPOFF32 ? (int32_t)slot : (int32_t)(address - (region + code->ends[x]));
            memcpy(region + code->fields[x], &value, sizeof(value));
        }
        if (mprotect(region, code_pages, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        unsigned long saved_fs = 0;
        if (data->tls_size != 0 && syscall(SYS_arch_prctl, ARCH_GET_FS, &saved_fs) != 0)
        {
            _exit(5);
        }
        typedef uint64_t (*Tier0TestEntry)(uint64_t, uint64_t, uint64_t);
        Tier0TestEntry entry = 0;
        void* entry_address = region + code->offsets[program->function_count - 1];
        memcpy(&entry, &entry_address, sizeof(entry));
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            tier0_test_initial(program, data, data_base);
            for (uint32_t g = 0; g < program->global_count; g += 1)
            {
                const uint32_t bytes = tier0_class_bytes(program->global_classes[g]) * program->global_elements[g];
                memcpy(tls + data->tls_offsets[g], data_base + data->offsets[g], tier0_test_thread_local(program, g) ? bytes : 0);
            }
            if (data->tls_size != 0)
            {
                syscall(SYS_arch_prctl, ARCH_SET_FS, self);
            }
            const uint64_t value = entry(inputs[r][0], inputs[r][1], inputs[r][2]);
            if (data->tls_size != 0)
            {
                syscall(SYS_arch_prctl, ARCH_SET_FS, saved_fs);
            }
            for (uint32_t g = 0; g < program->global_count; g += 1)
            {
                const uint32_t bytes = tier0_class_bytes(program->global_classes[g]) * program->global_elements[g];
                memcpy(data_base + data->offsets[g], tls + data->tls_offsets[g], tier0_test_thread_local(program, g) ? bytes : 0);
            }
            shared[r].result = value;
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
// What the programs' records exercise (#137), as tier0_emit_call, the prologue and the epilogue take them: arguments on
// the stack and ignored, each kind of part loaded (a general register's exactly, overlapping, through a temporary; an XMM
// register's exactly and through a temporary), results stored in registers and through a hidden pointer, parameters
// arriving in registers and on the stack, and functions returning records in registers and through a hidden pointer.
// And functions with a frame pointer (variable-length arrays, #136's item 8) that pass, take or return a record, whose
// homes the frame pointer addresses and whose calls' outgoing area is below what the body allocated.
#define BUSTER_TIER0_TEST_COVERAGE 15
static const char* const tier0_test_coverage_names[BUSTER_TIER0_TEST_COVERAGE] = {
    "stack arguments", "ignored arguments", "general exact", "general overlap", "general temporary", "xmm exact", "xmm temporary",
    "register results", "hidden results", "register parameters", "stack parameters", "returned in registers", "returned hidden", "empty arguments",
    "frame pointers with records",
};

static void tier0_test_coverage(const Tier0Functions* functions, uint64_t* coverage)
{
    for (uint32_t f = 0; f < functions->count; f += 1)
    {
        const Tier0Function* t = &functions->functions[f];
        bool records = t->result_size != 0 || t->result.kind == BUSTER_ABI_RESULT_MEMORY;
        bool pointer = false;
        for (uint32_t i = 0; i < t->count; i += 1)
        {
            pointer |= t->kinds[i] == BUSTER_TYPED_ALLOCA || t->kinds[i] == BUSTER_TYPED_STACK_SAVE || t->kinds[i] == BUSTER_TYPED_STACK_RESTORE;
        }
        for (uint32_t c = 0; c < t->call_count; c += 1)
        {
            for (uint32_t a = 0; a < t->call_argument_counts[c]; a += 1)
            {
                const uint32_t argument = t->call_first[c] + a;
                const uint32_t size = t->argument_sizes != 0 ? t->argument_sizes[argument] : 0;
                records |= size != 0;
                const AbiLocation location = t->argument_locations[argument];
                Tier0Part parts[2];
                const uint32_t count = size != 0 ? tier0_location_parts(location, size, parts) : 0;
                coverage[0] += size != 0 && location.kind == BUSTER_ABI_LOCATION_STACK ? 1 : 0;
                coverage[1] += size != 0 && location.kind == BUSTER_ABI_LOCATION_IGNORE ? 1 : 0;
                for (uint32_t k = 0; k < count; k += 1)
                {
                    const bool xmm = parts[k].reg != BUSTER_TIER0_PART_STACK && parts[k].reg >= 16;
                    coverage[xmm ? 5 + (parts[k].load == BUSTER_TIER0_PART_TEMPORARY ? 1 : 0) : 2 + parts[k].load] += parts[k].reg != BUSTER_TIER0_PART_STACK ? 1 : 0;
                }
            }
            const uint32_t object = t->call_result_objects != 0 ? t->call_result_objects[c] : 0;
            coverage[7] += object != 0 && t->calls[c].result.kind == BUSTER_ABI_RESULT_REGISTERS ? 1 : 0;
            coverage[8] += object != 0 && t->calls[c].result.kind == BUSTER_ABI_RESULT_MEMORY ? 1 : 0;
            records |= object != 0;
        }
        for (uint32_t p = 0; p < t->parameter_count; p += 1)
        {
            const bool object = tier0_is_object(t, p);
            coverage[9] += object && t->parameters[p].kind == BUSTER_ABI_LOCATION_REGISTERS ? 1 : 0;
            coverage[10] += object && t->parameters[p].kind == BUSTER_ABI_LOCATION_STACK ? 1 : 0;
            records |= object;
        }
        coverage[11] += t->result_size != 0 ? 1 : 0;
        coverage[12] += t->result.kind == BUSTER_ABI_RESULT_MEMORY ? 1 : 0;
        coverage[14] += pointer && records ? 1 : 0;
    }
}

static void tier0_test_programs(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, Tier0ProgramOptions options, uint64_t* records,
                                uint64_t* entries, uint64_t* deferred, uint64_t* deep, uint64_t* frames, uint64_t* coverage)
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
            code->deep = 0;
            code->realigned = 0;
            code->based = 0;
            if (ok)
            {
                tier0_test_coverage(functions, coverage);
                // An empty record's arguments: values (its address), whose location is IGNORE.
                for (uint32_t a = 0; program->argument_records != 0 && a < program->argument_count; a += 1)
                {
                    const uint32_t record = program->argument_records[a];
                    coverage[13] += record != BUSTER_TIER0_GENERATE_NO_RECORD && program->records->records[record].size == 0 ? 1 : 0;
                }
            }
            ok = ok && tier0_test_build(tally, functions, &arena, code, program_seed);
            *records += code->records;
            *entries += code->entries;
            *deferred += code->deferred;
            *deep += code->deep;
            frames[0] += code->realigned;
            frames[1] += code->based;
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

// Whether global g is defined in Tier 0's object of the cross-check: a direct one or a thread-local of the image's. The
// others, reached through the GOT, are the shared library's (tier0_test_library).
static bool tier0_test_defined(const Tier0Program* program, uint32_t g)
{
    return program->global_access[g] == BUSTER_TIER0_ACCESS_DIRECT || program->global_access[g] == BUSTER_TIER0_ACCESS_LOCAL_EXEC;
}

// Tier 0's code of a program as an ELF object: a TEXT atom per function and a DATA atom per global (its first values;
// TDATA for a thread-local) that the object defines (tier0_test_defined; the others are undefined symbols), named t0_f<k>,
// t0_entry and t0_g<k> (symbols in the streams' order: the globals, then the functions), with a fixup per symbolic field
// of the code's kind (CALL for a call, PC32 for a global's address or value, and the GOT's and TLS's kinds), each addend
// the field's distance from its instruction's end taken off for a kind measured from the field, as link_add_code_fixups
// makes them. A mixed object (#137's cross-linking) is named with `prefix` and defines only the even functions: the
// globals and the odd functions are references, which clang's build of the program defines, so that calls cross between
// the two both ways, with their arguments and results by value.
static bool tier0_test_object(const Tier0Program* program, const Tier0TestCode* code, const char* path, const char* prefix, bool mixed)
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
    // With `mixed`, what clang's object defines (the globals and the odd functions) is a reference here.
    for (uint32_t g = 0; result && g < globals; g += 1)
    {
        char name[32];
        const int length = snprintf(name, sizeof(name), "%sg%u", prefix, g);
        const uint8_t type = tier0_test_thread_local(program, g) ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
        result = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, type | BUSTER_LINK_SYMBOL_GLOBAL,
                                 !mixed && tier0_test_defined(program, g) ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE) == g + 1;
    }
    for (uint32_t f = 0; result && f < functions; f += 1)
    {
        char name[32];
        const int length = f + 1 == functions ? snprintf(name, sizeof(name), "%sentry", prefix) : snprintf(name, sizeof(name), "%sf%u", prefix, f);
        result = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL,
                                 mixed && f % 2 != 0 ? BUSTER_LINK_SOURCE_NONE : BUSTER_LINK_SOURCE_PROGRAM) == globals + 1 + f;
    }
    // The functions' atoms, each with its fixups (the code's fixups are in function order).
    uint32_t fixup = 0;
    for (uint32_t f = 0; result && f < functions; f += 1)
    {
        const uint32_t start = code->offsets[f];
        const uint32_t length = code->lengths[f];
        if (mixed && f % 2 != 0)
        {
            // clang's: this object only refers to it, and has none of its code.
            while (fixup < code->fixup_count && code->fields[fixup] < start + length)
            {
                fixup += 1;
            }
            continue;
        }
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
                const uint8_t kind = code->kinds[fixup];
                const int64_t addend = link_fixup_kinds[kind].pc_relative ? -(int64_t)(code->ends[fixup] - code->fields[fixup]) : 0;
                // The field's bytes are the fixup's, zero as GNU as writes them.
                memset(atom_bytes + (code->fields[fixup] - start), 0, 4);
                result = link_add_fixup(&link, atom, code->fields[fixup] - start, kind, target, addend) != 0;
                fixup += 1;
            }
        }
    }
    for (uint32_t g = 0; result && g < globals && !mixed; g += 1)
    {
        const uint32_t class_bytes = tier0_class_bytes(program->global_classes[g]);
        const uint32_t size = class_bytes * program->global_elements[g];
        uint8_t* atom_bytes = tier0_test_defined(program, g) ? arena_push(&bytes, size, 8) : 0;
        result = atom_bytes != 0 || !tier0_test_defined(program, g);
        if (atom_bytes != 0)
        {
            for (uint32_t e = 0; e < program->global_elements[g]; e += 1)
            {
                memcpy(atom_bytes + e * class_bytes, &program->global_values[program->global_first_value[g] + e], class_bytes);
            }
            const uint8_t class = tier0_test_thread_local(program, g) ? BUSTER_LINK_CLASS_TDATA : BUSTER_LINK_CLASS_DATA;
            const AtomId atom = link_add_atom(&link, class, 3, size, atom_bytes, 0);
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
// by Tier 0's, the results and every global compared, then each build timed over the inputs. With `mixed`, the mixed
// build too (m_: Tier 0's even functions and clang's odd ones), which must agree with clang's.
static bool tier0_test_harness(const Tier0Program* program, uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3], const char* path, bool mixed)
{
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    if (result)
    {
        fprintf(file, "#include <stdio.h>\n#include <string.h>\n#include <time.h>\n\n");
        fprintf(file, "unsigned long c_entry(unsigned long, unsigned long, unsigned long);\nunsigned long t0_entry(unsigned long, unsigned long, unsigned long);\n");
        if (mixed)
        {
            fprintf(file, "unsigned long m_entry(unsigned long, unsigned long, unsigned long);\n");
        }
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const char* type = tier0_c_type(program->global_classes[g]);
            const char* array = program->global_elements[g] != 1 ? "[16]" : "";
            const char* thread_local = tier0_test_thread_local(program, g) ? "_Thread_local " : "";
            fprintf(file, "extern %s%s c_g%u%s;\nextern %s%s t0_g%u%s;\nstatic unsigned char c_s%u[sizeof c_g%u], t0_s%u[sizeof t0_g%u];\n", thread_local, type, g,
                    array, thread_local, type, g, array, g, g, g, g);
            if (mixed)
            {
                fprintf(file, "extern %s%s m_g%u%s;\nstatic unsigned char m_s%u[sizeof m_g%u];\n", thread_local, type, g, array, g, g);
            }
        }
        fprintf(file, "static void reset(void)\n{\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            fprintf(file, "    memcpy(&c_g%u, c_s%u, sizeof c_s%u);\n    memcpy(&t0_g%u, t0_s%u, sizeof t0_s%u);\n", g, g, g, g, g, g);
            if (mixed)
            {
                fprintf(file, "    memcpy(&m_g%u, m_s%u, sizeof m_s%u);\n", g, g, g);
            }
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
            if (mixed)
            {
                fprintf(file, "    memcpy(m_s%u, &m_g%u, sizeof m_s%u);\n", g, g, g);
            }
        }
        fprintf(file, "    for (int r = 0; r < %u; r += 1)\n    {\n        reset();\n", BUSTER_TIER0_TEST_INPUTS);
        fprintf(file, "        unsigned long c = c_entry(inputs[r][0], inputs[r][1], inputs[r][2]);\n");
        fprintf(file, "        unsigned long t = t0_entry(inputs[r][0], inputs[r][1], inputs[r][2]);\n");
        fprintf(file, "        if (c != t)\n        {\n            printf(\"mismatch input %%d: clang %%016lx tier0 %%016lx\\n\", r, c, t);\n            failures += 1;\n        }\n");
        if (mixed)
        {
            fprintf(file, "        unsigned long m = m_entry(inputs[r][0], inputs[r][1], inputs[r][2]);\n");
            fprintf(file, "        if (c != m)\n        {\n            printf(\"mismatch input %%d: clang %%016lx mixed %%016lx\\n\", r, c, m);\n            failures += 1;\n        }\n");
        }
        for (uint32_t side = 0; side < (mixed ? 2u : 1u); side += 1)
        {
            const char* other = side == 0 ? "t0" : "m";
            for (uint32_t g = 0; g < program->global_count; g += 1)
            {
                const uint32_t class = program->global_classes[g];
                if (tier0_class_floating(class))
                {
                    const char* same = class == BUSTER_TYPED_CLASS_F32 ? "same_float" : "same_double";
                    fprintf(file, "        if (!%s((const void*)&c_g%u, (const void*)&%s_g%u, sizeof c_g%u / %u))\n", same, g, other, g, g, tier0_class_bytes(class));
                }
                else
                {
                    fprintf(file, "        if (memcmp(&c_g%u, &%s_g%u, sizeof c_g%u) != 0)\n", g, other, g, g);
                }
                fprintf(file, "        {\n            printf(\"mismatch input %%d: %s global %u\\n\", r);\n            failures += 1;\n        }\n", other, g);
            }
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

// The shared library of the globals Tier 0's object leaves undefined (tier0_test_defined), those it reaches through the
// GOT: each its first values' bytes, a thread-local in .tdata, so that the executable's GOT slots are the loader's to fill
// (GLOB_DAT, or TPOFF64 for a thread-local's offset), as a shared library's data is. It is GNU as source, which as and
// ld -shared make into the library in a few milliseconds, where clang -shared of the same data as C took tens.
static bool tier0_test_library(const Tier0Program* program, const char* path)
{
    FILE* file = fopen(path, "w");
    bool result = file != 0;
    if (result)
    {
        fprintf(file, "\t.section .note.GNU-stack,\"\",@progbits\n");
        for (uint32_t g = 0; g < program->global_count; g += 1)
        {
            const uint32_t class_bytes = tier0_class_bytes(program->global_classes[g]);
            const uint32_t size = class_bytes * program->global_elements[g];
            if (!tier0_test_defined(program, g))
            {
                const bool thread_local = tier0_test_thread_local(program, g);
                fprintf(file, "\t%s\n\t.globl t0_g%u\n\t.type t0_g%u, %s\n\t.size t0_g%u, %u\n\t.balign 8\nt0_g%u:\n\t.byte",
                        thread_local ? ".section .tdata,\"awT\",@progbits" : ".data", g, g, thread_local ? "@tls_object" : "@object", g, size, g);
                for (uint32_t b = 0; b < size; b += 1)
                {
                    const uint64_t value = program->global_values[program->global_first_value[g] + b / class_bytes];
                    fprintf(file, "%s%u", b == 0 ? " " : ", ", (uint32_t)(value >> (8 * (b % class_bytes)) & 0xff));
                }
                fprintf(file, "\n");
            }
        }
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

// `clang -O0 [-fwrapv -fno-strict-aliasing] -w -c SOURCE -o OBJECT` through the oracle cache (oracle_cache.c, #542),
// with `dialect` the repository's (the program's C) or not (the harness's), what it prints on the errors: whether it
// compiled.
static bool tier0_test_compile(const char* clang, bool dialect, const char* source, const char* object)
{
    char* program[] = { (char*)clang, "-O0", "-fwrapv", "-fno-strict-aliasing", "-w", "-c", (char*)source, "-o", (char*)object, 0 };
    char* harness[] = { (char*)clang, "-O0", "-w", "-c", (char*)source, "-o", (char*)object, 0 };
    const char* files[] = { object };
    OracleRun run = {
        .purpose = "tier0: clang -O0 -c",
        .arguments = dialect ? program : harness,
        .files = files,
        .file_count = 1,
        .dependencies = true,
    };
    const bool ran = oracle_run(&run);
    fputs(run.output != 0 ? run.output : "", stderr);
    fputs(run.errors != 0 ? run.errors : "", stderr);
    const bool result = ran && WIFEXITED(run.status) && WEXITSTATUS(run.status) == 0;
    oracle_run_release(&run);
    return result;
}

// The cross-check: `count` programs, each compiled by clang -O0 (as C) and by Tier 0 (as an object), linked into one
// harness that runs both on the same inputs; their results and globals must agree. Also reports the code's sizes (clang's
// .text, Tier 0's functions) and the harness's times. With records (#137), each program is also linked mixed, its even
// functions Tier 0's and its odd ones clang's (whose own copies of the even ones objcopy makes weak), so that its calls
// pass and return structs and unions by value between the two compilers both ways; it must agree with clang's build too.
static void tier0_test_cross(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, Tier0ProgramOptions options, const char* clang,
                             bool keep)
{
    const bool mixed = options.records != 0;
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
            char library[600];
            char executable[600];
            char mixed_source[600];
            char mixed_object[600];
            snprintf(source, sizeof(source), "%s/program_%u.c", directory, p);
            snprintf(object, sizeof(object), "%s/tier0_%u.o", directory, p);
            snprintf(harness, sizeof(harness), "%s/harness_%u.c", directory, p);
            snprintf(library, sizeof(library), "%s/library_%u.s", directory, p);
            snprintf(executable, sizeof(executable), "%s/run_%u", directory, p);
            snprintf(mixed_source, sizeof(mixed_source), "%s/mixed_%u.c", directory, p);
            snprintf(mixed_object, sizeof(mixed_object), "%s/tier0_mixed_%u.o", directory, p);
            if (ok)
            {
                FILE* file = fopen(source, "w");
                ok = file != 0 && fwrite(program->text, 1, program->text_length, file) == program->text_length;
                ok = file != 0 && fclose(file) == 0 && ok;
                uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
                tier0_test_inputs(program_seed, inputs);
                ok = ok && tier0_test_object(program, code, object, "t0_", false) && tier0_test_harness(program, inputs, harness, mixed) &&
                     tier0_test_library(program, library);
                if (ok && mixed)
                {
                    // The same program's C named m_, and Tier 0's mixed object.
                    Tier0Program* named = BUSTER_ARENA_PUSH_ARRAY(&arena, Tier0Program, 1);
                    ok = named != 0 && tier0_generate_program(named, &arena, program_seed, options, "m_") && tier0_test_object(program, code, mixed_object, "m_", true);
                    file = ok ? fopen(mixed_source, "w") : 0;
                    ok = file != 0 && fwrite(named->text, 1, named->text_length, file) == named->text_length;
                    ok = file != 0 && fclose(file) == 0 && ok;
                }
                ok = tier0_test_expect(tally, ok, "cross-check", "seed %" PRIu64 ": the files can't be written", program_seed);
            }
            if (ok)
            {
                char command[8192];
                char line[512];
                // -fwrapv and -fno-strict-aliasing, this repository's own dialect: signed arithmetic wraps, as Tier 0's
                // does, and the objects' checksums read floating parts' bits through integer lvalues. The library of the
                // globals Tier 0 reaches through the GOT is a shared library the executable finds by its run path, data
                // alone, which GNU as assembles and ld links without the C runtime's files.
                // clang's compiles go through the oracle cache (tier0_test_compile), the harness's on its own; the rest, which
                // reads Tier 0's objects, runs every time.
                char program_object[620];
                char harness_object[620];
                char mixed_program_object[620];
                snprintf(program_object, sizeof(program_object), "%s.o", source);
                snprintf(harness_object, sizeof(harness_object), "%s.o", harness);
                snprintf(mixed_program_object, sizeof(mixed_program_object), "%s.o", mixed_source);
                const bool compiled = tier0_test_compile(clang, true, source, program_object) && (!mixed || tier0_test_compile(clang, true, mixed_source, mixed_program_object)) &&
                                tier0_test_compile(clang, false, harness, harness_object);
                snprintf(command, sizeof(command), "as -o %s.o %s 2>&1 && ld -shared -o %s.so %s.o 2>&1 && %s -O0 -w -o %s %s %s %s %s.so -Wl,-rpath,%s 2>&1", library,
                         library, library, library, clang, executable, harness_object, program_object, object, library, directory);
                if (mixed)
                {
                    // The mixed build's: clang's object with its even functions weak, Tier 0's even ones taking their
                    // place in the link. Its globals are all clang's object's, those Tier 0 reaches through the GOT too.
                    char weaken[2048];
                    uint64_t length = 0;
                    weaken[0] = 0;
                    for (uint32_t f = 0; f < program->function_count && length + 32 < sizeof(weaken); f += 2)
                    {
                        const int written = f + 1 == program->function_count ? snprintf(weaken + length, sizeof(weaken) - length, " --weaken-symbol=m_entry")
                                                                             : snprintf(weaken + length, sizeof(weaken) - length, " --weaken-symbol=m_f%u", f);
                        length += written > 0 ? (uint64_t)written : 0;
                    }
                    snprintf(command, sizeof(command),
                             "as -o %s.o %s 2>&1 && ld -shared -o %s.so %s.o 2>&1 && objcopy%s %s 2>&1 && %s -O0 -w -o %s %s %s %s %s %s %s.so -Wl,-rpath,%s 2>&1",
                             library, library, library, library, weaken, mixed_program_object, clang, executable, harness_object, program_object, object,
                             mixed_program_object, mixed_object, library, directory);
                }
                ok = tier0_test_expect(tally, compiled && tier0_test_shell(command, 0, line, sizeof(line)) == 0, "cross-check", "seed %" PRIu64 ": can't compile or link (%s)",
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

// The errors both paths must report alike, on streams of the test's own: the first entry without a template (NOT of a
// double, which tier0_validate refuses too: a stream the paths take covers every entry) wherever it falls among
// 64-entry blocks and after another, and an expression deeper than the integer depths.
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
        // `place` constants discarded, an entry without a template, more constants, another, the return.
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
            classes[count] = BUSTER_TYPED_CLASS_F64;
            values[count] = c + 1;
            kinds[count + 1] = BUSTER_TYPED_NOT;
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
                          "errors", "the entry at %u: the scalar path's error %d at %u, the vector path's %d at %u", expected, scalar.error, scalar.node,
                          vector.error, vector.node);
        arena_pop_to(&arena, mark);
    }
    static const uint32_t deeps[] = { 14, 15, 16, 17, 32, 239, 240, 241, 250 };
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(deeps); d += 1)
    {
        // `deep` constants on the integer stack at once, added up and discarded. The last one is the first add's
        // immediate, so 16 fit the 15 depths, the next ones go to the deep slots, and a stack of 240 (241 constants) is
        // refused past BUSTER_TIER0_MAX_DEPTH.
        const uint32_t deep = deeps[d];
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
        const Tier0Error expected = deep - 1 >= BUSTER_TIER0_MAX_DEPTH ? TIER0_ERROR_DEPTH : TIER0_OK;
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
// address. A frame with an object aligned past 16 is realigned: rbp pushed and pointed at the push, the frame reserved
// and rsp rounded down to the most an object needs, which the frame publishes, so that its places are rsp's.
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
    for (uint32_t way = 0; way < 11; way += 1)
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
        static const char* const ways[11] = {
            "kept",
            "a class",
            "not address-taken",
            "a parameter",
            "aligned past 2^30",
            "sizes without alignments",
            "read as a value",
            "past BUSTER_TIER0_MAX_OBJECT_BYTES",
            "BUSTER_TIER0_MAX_OBJECT_BYTES exactly",
            "aligned to 2^30",
            "past BUSTER_TIER0_MAX_OBJECT_BYTES by its alignment",
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
            local_alignments[0] = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 + 1;
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
        case 9:
            // Its size rounded up to 16, and its alignment less 16: the limit, exactly. (Not compiled: its frame is 1 GB.)
            local_sizes[0] = 16;
            local_alignments[0] = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2;
            local_sizes[1] = 0;
            function.local_count = 1;
            break;
        case 10:
            local_sizes[0] = 17;
            local_alignments[0] = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2;
            local_sizes[1] = 0;
            function.local_count = 1;
            break;
        default:
            break;
        }
        const bool valid = way == 0 || way == 8 || way == 9;
        const Tier0Check check = tier0_validate(&function);
        tier0_test_expect(tally, (check.error == TIER0_OK) == valid && (valid || check.node == 0), "objects", "an object %s: tier0_validate says %s at %u (%s)",
                          ways[way], check.error == TIER0_OK ? "valid" : "invalid", check.node, check.rule != 0 ? check.rule : "");
        if (valid && way != 9)
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
    // displacement its place. In every other round, a quarter of the objects are aligned past 16 instead, to 32, 64, 128
    // or 4096 (from a sequence of their own, so that the other rounds' frames are the ones they were).
    uint64_t random = seed ^ 0x6f626a656374ull;
    uint64_t past = seed ^ 0x7265616c69676eull;
    static const uint8_t past_alignments[4] = { 5, 6, 7, 12 };
    for (uint32_t round = 0; round < 2000; round += 1)
    {
        const uint32_t locals = 1 + tier0_generate_below(&random, locals_capacity);
        uint32_t count = 0;
        bool sixteen = false;
        uint32_t realigned = 0;
        for (uint32_t l = 0; l < locals; l += 1)
        {
            const bool object = tier0_generate_chance(&random, 60);
            const bool va_list = !object && tier0_generate_chance(&random, 20);
            local_sizes[l] = object ? 1 + tier0_generate_below(&random, 300) : 0;
            local_alignments[l] = object ? (uint8_t)tier0_generate_below(&random, 5) : 0;
            if (object && round % 2 == 1 && tier0_generate_chance(&past, 25))
            {
                local_alignments[l] = past_alignments[tier0_generate_below(&past, 4)];
                realigned = local_alignments[l] > realigned ? local_alignments[l] : realigned;
            }
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
        // The prologue is the frame's sub alone (nothing is pushed), or in a realigned frame rbp's push and mov, the sub
        // and the and of rsp, then a lea per address-taken local, in order.
        const uint32_t reserve = realigned != 0 ? 2 : 0;
        laid = laid && (realigned == 0 || (scalar.frame_pointer && scalar.frame_alignment_log2 == realigned && !scalar.frame_base && scalar.count > 4 &&
                                           scalar.records[0].form == X86_FORM_PUSH_R64 && scalar.records[0].rm == X86_REGISTER_RBP &&
                                           scalar.records[1].form == X86_FORM_MOV_RM64_R64 && scalar.records[1].rm == X86_REGISTER_RBP &&
                                           (scalar.records[3].form == X86_FORM_AND_RM64_IMM8 || scalar.records[3].form == X86_FORM_AND_RM64_IMM32) &&
                                           scalar.records[3].rm == X86_REGISTER_RSP && (int64_t)scalar.records[3].immediate == -(int64_t)(UINT64_C(1) << realigned)));
        laid = laid && (realigned != 0 || (!scalar.frame_pointer && scalar.frame_alignment_log2 == 0));
        const uint32_t size = laid && scalar.count > reserve && (scalar.records[reserve].form & ~BUSTER_X86_FORM_FIXUP) != X86_FORM_RET ? (uint32_t)scalar.records[reserve].immediate : 0;
        const uint32_t first = reserve + (size != 0 ? 1 : 0) + (realigned != 0 ? 1 : 0);
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
        laid &= !sixteen || realigned != 0 || (size + 8) % 16 == 0;
        tier0_test_expect(tally, laid, "objects", "round %u: %u locals, a frame of %u bytes (realigned to 2^%u): the paths disagree, or a local is misplaced", round, locals,
                          size, realigned);
        arena_pop_to(&arena, mark);
    }
    arena_release(&arena);
}

// The full stable order's key: descending unsigned reference count, then ascending local identity. Unlike the
// production bounded prefixes, this oracle sorts all locals. Small cases also check the old insertion sort exactly.
static int tier0_test_home_key_compare(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
}

static void tier0_test_home_oracle(const Tier0Function* function, Tier0Frame* frame, Tier0Home* homes, const uint32_t* references, const uint64_t* keys)
{
    uint32_t taken = frame->clobbers | 1u << X86_REGISTER_RSP | (frame->pointer ? 1u << X86_REGISTER_RBP : 0) | (frame->based ? 1u << X86_REGISTER_R15 : 0);
    memset(homes, 0, (uint64_t)function->local_count * sizeof(*homes));
    for (uint32_t k = 0; k < function->local_count; k += 1)
    {
        const uint32_t local = (uint32_t)keys[k];
        const bool eligible = references[local] != 0 && (function->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0;
        uint32_t chosen = UINT32_MAX;
        const bool floating = tier0_class_floating(function->local_classes[local]);
        for (uint32_t x = 0; x < 16 && floating && eligible && chosen == UINT32_MAX; x += 1)
        {
            chosen = (taken & 1u << BUSTER_TIER0_XMM(x)) == 0 ? x : UINT32_MAX;
        }
        for (uint32_t d = 0; d < BUSTER_TIER0_CALLER_SAVED_DEPTHS && !floating && eligible && chosen == UINT32_MAX; d += 1)
        {
            const uint32_t reg = tier0_integer_depths[d];
            chosen = (taken & 1u << reg) == 0 ? reg : UINT32_MAX;
        }
        for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED && !floating && eligible && references[local] >= 2 && chosen == UINT32_MAX; c += 1)
        {
            const uint32_t reg = tier0_callee_saved[c];
            if ((taken & 1u << reg) == 0)
            {
                chosen = reg;
                frame->saved |= 1u << c;
            }
        }
        if (chosen != UINT32_MAX)
        {
            homes[local].is_register = 1;
            homes[local].reg = (uint8_t)chosen;
            taken |= 1u << (floating ? BUSTER_TIER0_XMM(chosen) : chosen);
        }
    }
    for (uint32_t c = 0; c < BUSTER_TIER0_CALLEE_SAVED; c += 1)
    {
        frame->saved |= (frame->clobbers & 1u << tier0_callee_saved[c]) != 0 ? 1u << c : 0;
    }
    frame->saved |= frame->based ? 1u << BUSTER_TIER0_SAVED_R15 : 0;
    frame->pushes = (uint32_t)__builtin_popcount(frame->saved) + (frame->pointer ? 1u : 0u);
}

// All columns end at protected pages: tails around 16 and 64, then up to 100,000 locals. The keys cover ties, zeros,
// arbitrary u32 counts, increasing/decreasing orders and the alternating references builtin recipes produce. Check
// each prefix against the full order, then both complete home assignments against the original greedy policy.
static void tier0_test_home_candidates(Tier0TestTally* tally, uint64_t seed)
{
    const uint32_t maximum = 100000;
    const uint64_t span = ((uint64_t)maximum * sizeof(uint64_t) + BUSTER_TIER0_TEST_PAGE - 1) / BUSTER_TIER0_TEST_PAGE * BUSTER_TIER0_TEST_PAGE;
    const uint64_t stride = span + BUSTER_TIER0_TEST_PAGE;
    uint8_t* pages = mmap(0, 7 * stride, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guarded = pages != MAP_FAILED;
    for (uint32_t column = 0; column < 7 && guarded; column += 1)
    {
        guarded = mprotect(pages + column * stride + span, BUSTER_TIER0_TEST_PAGE, PROT_NONE) == 0;
    }
    tier0_test_expect(tally, guarded, "home candidates", "no guarded columns");
    uint64_t random = seed ^ UINT64_C(0x363634686f6d6573);
    static const uint32_t sizes[] = { 81, 127, 255, 1024, 4097, 16384, 65536, 100000 };
    for (uint32_t test = 0; test < 81 + BUSTER_ARRAY_LENGTH(sizes) && guarded; test += 1)
    {
        const uint32_t locals = test < 81 ? test : sizes[test - 81];
        uint32_t* references = (uint32_t*)(pages + span) - locals;
        uint8_t* classes = pages + stride + span - locals;
        uint8_t* flags = pages + 2 * stride + span - locals;
        Tier0Home* expected = (Tier0Home*)(pages + 3 * stride + span) - locals;
        Tier0Home* scalar = (Tier0Home*)(pages + 4 * stride + span) - locals;
        Tier0Home* vector = (Tier0Home*)(pages + 5 * stride + span) - locals;
        uint64_t* keys = (uint64_t*)(pages + 6 * stride + span) - locals;
        for (uint32_t pattern = 0; pattern < 10; pattern += 1)
        {
            for (uint32_t local = 0; local < locals; local += 1)
            {
                static const uint32_t recipe[] = { 2, 26, 17, 1 };
                const uint32_t generated = tier0_generate_below(&random, UINT32_MAX);
                references[local] = pattern == 0 ? 0 : pattern == 1 ? 1 : pattern == 2 ? 2 : pattern == 3 ? local + 1 :
                                    pattern == 4 ? locals - local : pattern == 5 ? recipe[local % 4] : pattern == 6 ? generated % 7 :
                                    pattern == 7 ? UINT32_MAX - generated % 7 : pattern == 8 ? generated : (local % 97 == 0 ? generated : 0);
                classes[local] = pattern == 3 || pattern == 5 ? BUSTER_TYPED_CLASS_I64 : pattern == 4 ? BUSTER_TYPED_CLASS_F64 :
                                 local % 3 == 0 ? BUSTER_TYPED_CLASS_F32 : local % 3 == 1 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_I64;
                flags[local] = pattern % 2 == 0 && generated % 5 == 0 ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
                keys[local] = (uint64_t)(UINT32_MAX - references[local]) << 32 | local;
                expected[local] = (Tier0Home){ .is_register = (uint8_t)(pattern % 3 == 0 && generated % 7 == 0) };
            }
            qsort(keys, locals, sizeof(*keys), tier0_test_home_key_compare);
            uint32_t old[1024];
            for (uint32_t local = 0; local < locals && locals <= BUSTER_ARRAY_LENGTH(old); local += 1)
            {
                uint32_t at = local;
                while (at > 0 && references[old[at - 1]] < references[local])
                {
                    old[at] = old[at - 1];
                    at -= 1;
                }
                old[at] = local;
            }
            bool stable = true;
            for (uint32_t local = 0; local < locals && locals <= BUSTER_ARRAY_LENGTH(old); local += 1)
            {
                stable &= old[local] == (uint32_t)keys[local];
            }
            tier0_test_expect(tally, stable, "home candidates", "%u locals, pattern %u: full-order oracle differs from old stable sort", locals, pattern);
            Tier0Function function = { .local_count = locals, .local_classes = classes, .local_flags = flags };
            uint32_t wanted[2 * BUSTER_TIER0_HOME_CANDIDATES];
            uint32_t count = 0;
            for (uint32_t kind = 0; kind < 2; kind += 1)
            {
                uint32_t kept = 0;
                for (uint32_t k = 0; k < locals && kept < BUSTER_TIER0_HOME_CANDIDATES; k += 1)
                {
                    const uint32_t local = (uint32_t)keys[k];
                    if (references[local] != 0 && expected[local].is_register == 0 && (flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) == 0 &&
                        tier0_class_floating(classes[local]) == (kind != 0))
                    {
                        wanted[count] = local;
                        count += 1;
                        kept += 1;
                    }
                }
            }
            uint32_t orders[2][2 * BUSTER_TIER0_HOME_CANDIDATES + 1];
            memset(orders, 0x9d, sizeof(orders));
            const uint32_t scalar_count = tier0_register_locals(&function, expected, references, orders[0]);
            const uint32_t vector_count = tier0_register_locals_vector(&function, expected, references, orders[1]);
            const bool same = scalar_count == count && vector_count == count && memcmp(wanted, orders[0], count * sizeof(*wanted)) == 0 &&
                              memcmp(wanted, orders[1], count * sizeof(*wanted)) == 0 && orders[0][count] == UINT32_C(0x9d9d9d9d) && orders[1][count] == UINT32_C(0x9d9d9d9d);
            tier0_test_expect(tally, same, "home candidates", "%u locals, pattern %u: eligible prefixes differ", locals, pattern);
            Tier0Frame frame = { .clobbers = pattern % 3 == 0 ? 0 : pattern % 3 == 1 ? UINT32_MAX : tier0_generate_below(&random, UINT32_MAX),
                                 .pointer = pattern % 2 != 0, .based = pattern % 4 == 3 };
            Tier0Frame scalar_frame = frame;
            Tier0Frame vector_frame = frame;
            tier0_test_home_oracle(&function, &frame, expected, references, keys);
            tier0_assign_homes(&function, &scalar_frame, scalar, references, orders[0], false);
            tier0_assign_homes(&function, &vector_frame, vector, references, orders[1], true);
            const bool homes = memcmp(expected, scalar, (uint64_t)locals * sizeof(*expected)) == 0 && memcmp(expected, vector, (uint64_t)locals * sizeof(*expected)) == 0 &&
                               frame.saved == scalar_frame.saved && frame.saved == vector_frame.saved && frame.pushes == scalar_frame.pushes && frame.pushes == vector_frame.pushes;
            tier0_test_expect(tally, homes, "home candidates", "%u locals, pattern %u: complete homes or save masks differ from greedy oracle", locals, pattern);
        }
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, 7 * stride);
    }
}

// The frame's alignment, tier0_frame_alignment and its vector twin, against the test's own most: for every count of
// locals to 40 and of calls to 12, random columns that each end at a PROT_NONE page, so that a read past one faults.
// Sizes of 0 among the locals (not objects, whatever their alignment byte), alignments past 16 now and then (any byte,
// up to 255), and calls whose every other byte is random too; then the same without local_sizes, the calls' alone.
static void tier0_test_frame_alignment(Tier0TestTally* tally, uint64_t seed)
{
    uint8_t* pages = mmap(0, 6 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guarded = pages != MAP_FAILED;
    for (uint32_t g = 1; g < 6 && guarded; g += 2)
    {
        guarded = mprotect(pages + g * BUSTER_TIER0_TEST_PAGE, BUSTER_TIER0_TEST_PAGE, PROT_NONE) == 0;
    }
    tier0_test_expect(tally, guarded, "frame alignment", "no pages between PROT_NONE ones");
    uint64_t random = seed ^ 0x616c69676e6d656eull;
    for (uint32_t locals = 0; locals <= 40 && guarded; locals += 1)
    {
        for (uint32_t calls = 0; calls <= 12; calls += 1)
        {
            uint32_t* sizes = (uint32_t*)(pages + BUSTER_TIER0_TEST_PAGE) - locals;
            uint8_t* alignments = pages + 3 * BUSTER_TIER0_TEST_PAGE - locals;
            AbiCall* call_column = (AbiCall*)(pages + 5 * BUSTER_TIER0_TEST_PAGE) - calls;
            uint32_t objects_most = BUSTER_TIER0_FRAME_ALIGNMENT_LOG2;
            uint32_t calls_most = BUSTER_TIER0_FRAME_ALIGNMENT_LOG2;
            for (uint32_t l = 0; l < locals; l += 1)
            {
                sizes[l] = tier0_generate_chance(&random, 30) ? 0 : 1 + tier0_generate_below(&random, 1000);
                alignments[l] = (uint8_t)(tier0_generate_chance(&random, 90) ? tier0_generate_below(&random, 5) : tier0_generate_below(&random, 256));
                objects_most = sizes[l] != 0 && alignments[l] > objects_most ? alignments[l] : objects_most;
            }
            for (uint32_t c = 0; c < calls; c += 1)
            {
                uint8_t bytes[sizeof(AbiCall)];
                for (uint32_t b = 0; b < sizeof(AbiCall); b += 1)
                {
                    bytes[b] = (uint8_t)tier0_generate_below(&random, 256);
                }
                memcpy(&call_column[c], bytes, sizeof(AbiCall));
                call_column[c].stack_alignment_log2 = tier0_generate_chance(&random, 90) ? (uint8_t)tier0_generate_below(&random, 5) : call_column[c].stack_alignment_log2;
                calls_most = call_column[c].stack_alignment_log2 > calls_most ? call_column[c].stack_alignment_log2 : calls_most;
            }
            for (uint32_t with_sizes = 0; with_sizes < 2; with_sizes += 1)
            {
                Tier0Function function;
                memset(&function, 0, sizeof(function));
                function.local_count = locals;
                function.local_sizes = with_sizes != 0 ? sizes : 0;
                function.local_alignments = alignments;
                function.calls = call_column;
                function.call_count = calls;
                const uint32_t most = with_sizes != 0 && objects_most > calls_most ? objects_most : calls_most;
                const uint32_t expected = most > BUSTER_TIER0_FRAME_ALIGNMENT_LOG2 ? most : 0;
                const uint32_t scalar = tier0_frame_alignment(&function);
                const uint32_t vector = tier0_frame_alignment_vector(&function);
                tier0_test_expect(tally, scalar == expected && vector == expected, "frame alignment", "%u locals (%s), %u calls: 2^%u expected, the paths give %u and %u",
                                  locals, with_sizes != 0 ? "with sizes" : "no sizes", calls, expected, scalar, vector);
            }
        }
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, 6 * BUSTER_TIER0_TEST_PAGE);
    }
}

// A copy's scratch register, its stacks' first free one (an XMM one from 16 bytes, a general one below), must be in
// the frame's clobbers after either selection, so that no local makes it its home; past the tables, selection says the
// function is too deep, and the compile takes the values to the deep slots. On copies above every count of integer and
// floating values, of each size class, with both paths' code compared. The values are then folded in postorder, each
// consumer's last operand the entry before it: the doubles summed onto a zero pushed above them, the sum converted, and
// the integers summed onto that. The zero takes one more floating depth, so that 16 doubles are too deep for the
// tables whatever the copy.
static void tier0_test_copies(Tier0TestTally* tally)
{
    enum
    {
        capacity = 96,
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
                // The values, the copy between the objects, then the values folded and the sum discarded.
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
                const uint32_t folds = floatings != 0 ? floatings + 2 : integers != 0 ? 1 : 0;
                for (uint32_t k = 0; k < folds; k += 1)
                {
                    // A zero (a double's, past doubles), the doubles' sums, and their sum converted to an integer.
                    kinds[count] = k == 0 ? BUSTER_TYPED_CONST : k + 1 == folds && floatings != 0 ? BUSTER_TYPED_CONVERT : BUSTER_TYPED_ADD;
                    classes[count] = floatings != 0 && k + 1 < folds ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_I64;
                    values[count] = 0;
                    count += 1;
                }
                for (uint32_t k = 0; k < integers; k += 1)
                {
                    kinds[count] = BUSTER_TYPED_ADD;
                    classes[count] = BUSTER_TYPED_CLASS_I64;
                    values[count] = 0;
                    count += 1;
                }
                if (integers + floatings != 0)
                {
                    kinds[count] = BUSTER_TYPED_DISCARD;
                    classes[count] = BUSTER_TYPED_CLASS_I64;
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
                const bool too_deep = floatings >= BUSTER_TIER0_FLOATING_DEPTHS || (wide ? floating >= BUSTER_TIER0_FLOATING_DEPTHS : depth >= BUSTER_TIER0_INTEGER_DEPTHS);
                const uint32_t scratch = too_deep ? 0 : wide ? 1u << BUSTER_TIER0_XMM(floating) : 1u << tier0_integer_depths[depth];
                bool counted = ready;
                for (uint32_t path = 0; path < 2 && ready; path += 1)
                {
                    counted &= too_deep ? errors[path] == TIER0_ERROR_DEPTH : errors[path] == TIER0_OK && (frames[path].clobbers & scratch) != 0;
                }
                // Past the tables, the copy's values go to the deep slots (tier0_select_deep).
                const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
                const Tier0Code vector = tier0_compile(&function, &arena);
                const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector);
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

// The conversions between U64 and the floating classes (tier0_emit_u64_convert) on named values, as functions that
// convert their parameter, compiled by both paths and run: from U64 around 2^53, 2^63 and 2^64, where the halving's
// lowest bit decides the rounding (ties to even, and just past them), and from values below 2^63, at it and up to the
// largest below 2^64, each against C's conversion of it. Each call must raise the floating exceptions C's conversion
// raises, MXCSR's invalid and inexact flags (#429's review: a truncation of a value at 2^63 or past it, which C's
// conversion takes, is invalid as a signed one, and a needless subtraction inexact), and every conversion to U64 must run
// with invalid unmasked, where a spurious one would raise SIGFPE.
#define BUSTER_TIER0_TEST_UNSIGNED_VALUES 22
#define BUSTER_TIER0_TEST_UNSIGNED_FLAGS (_MM_EXCEPT_INVALID | _MM_EXCEPT_INEXACT)
// MXCSR's four rounding modes: to nearest, down, up and toward zero.
#define BUSTER_TIER0_TEST_UNSIGNED_MODES 4

// C's conversion of value v for signature `signature` (tier0_test_unsigned_floating's order) under the rounding mode MXCSR
// holds, as clang compiles it: its bits into *bits, and the flags it raised returned. The oracle, out of line, with the
// floating environment's accesses kept in order and its rounding mode honored.
static __attribute__((noinline)) uint32_t tier0_test_unsigned_oracle(uint32_t signature, uint64_t integer, double d, float f, uint64_t* bits)
{
#pragma STDC FENV_ACCESS ON
    volatile uint64_t integer_in = integer;
    volatile double double_in = d;
    volatile float float_in = f;
    _mm_setcsr(_mm_getcsr() & ~(uint32_t)_MM_EXCEPT_MASK);
    uint64_t result = 0;
    if (signature == 0)
    {
        volatile double out = (double)integer_in;
        const double value = out;
        memcpy(&result, &value, sizeof(value));
    }
    else if (signature == 1)
    {
        volatile float out = (float)integer_in;
        const float value = out;
        uint32_t low = 0;
        memcpy(&low, &value, sizeof(value));
        result = low;
    }
    else if (signature == 2)
    {
        volatile uint64_t out = (uint64_t)double_in;
        result = out;
    }
    else
    {
        volatile uint64_t out = (uint64_t)float_in;
        result = out;
    }
    const uint32_t flags = _mm_getcsr() & BUSTER_TIER0_TEST_UNSIGNED_FLAGS;
    *bits = result;
    return flags;
}

// The named values, of each signature's operand class.
static const uint64_t tier0_test_unsigned_integers[BUSTER_TIER0_TEST_UNSIGNED_VALUES] = {
    0, 1, (1ull << 53) - 1, (1ull << 53) + 1, (1ull << 53) + 3, 1ull << 62, (1ull << 63) - 1, 1ull << 63, (1ull << 63) + 1,
    0x8000000000000400ull, 0x8000000000000401ull, 0x8000000000000c00ull, 0x8000008000000000ull, 0x8000008000000001ull,
    0xfffffffffffffbffull, 0xfffffffffffffc00ull, 0xffffff7fffffffffull, 0xffffff8000000000ull, UINT64_MAX, 0x0123456789abcdefull,
    0x8000000000000c01ull, 0x10000000000003ffull,
};
static const double tier0_test_unsigned_doubles[BUSTER_TIER0_TEST_UNSIGNED_VALUES] = {
    0.0, 0.5, 1.0, 1.75, 4503599627370497.0, 0x1p62, 0x1p63 - 1024.0, 0x1p63, 0x1p63 + 2048.0, 0x1p64 - 2048.0,
    1e19, 12345.678, 0x1.fffffffffffffp62, 0x1.0000000000001p63, 0x1.8p63, 9007199254740993.0, 3.0e18, 0x1p63 - 2048.0,
    0x1.23456789abcdep63, 0.999, -0.5, -0.0,
};
static const float tier0_test_unsigned_floats[BUSTER_TIER0_TEST_UNSIGNED_VALUES] = {
    0.0f, 0.5f, 1.0f, 3.5f, 0x1p24f, 0x1.fffffep62f, 0x1p63f, 0x1.000002p63f, 0x1.fffffep63f, 1e19f, 16777217.0f, 0x1p40f,
    0x1.8p63f, 0x1.4p63f, 12345.678f, 0x1p62f, 0x1.000002p62f, 0x1p53f, 0.999f, 0x1.234566p63f, -0.5f, -0.0f,
};
// The signatures: each one's name, and the classes it converts to and from.
static const char* const tier0_test_unsigned_names[4] = { "to double", "to float", "from double", "from float" };
static const uint8_t tier0_test_unsigned_to[4] = { BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F32, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
static const uint8_t tier0_test_unsigned_from[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_F32 };

static void tier0_test_unsigned_floating(Tier0TestTally* tally)
{
    const uint64_t* integers = tier0_test_unsigned_integers;
    const double* doubles = tier0_test_unsigned_doubles;
    const float* floats = tier0_test_unsigned_floats;
    const char* const* names = tier0_test_unsigned_names;
    const uint8_t* to = tier0_test_unsigned_to;
    const uint8_t* from = tier0_test_unsigned_from;
    // Each signature's code by each path, a page each; per rounding mode, the results and then the flags each call raised,
    // the child's.
    uint64_t* shared = mmap(0, sizeof(uint64_t) * 16 * BUSTER_TIER0_TEST_UNSIGNED_VALUES * BUSTER_TIER0_TEST_UNSIGNED_MODES, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* region = mmap(0, 8 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1u << 20);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && region != MAP_FAILED && arena.base != 0, "unsigned floating", "no memory");
    for (uint32_t signature = 0; signature < 4 && ready; signature += 1)
    {
        const uint8_t kinds[3] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONVERT, BUSTER_TYPED_RETURN };
        const uint8_t classes[3] = { from[signature], to[signature], to[signature] };
        const uint64_t values[3] = { 0, 0, 0 };
        const uint8_t local_classes[1] = { from[signature] };
        const uint8_t local_flags[1] = { 0 };
        const bool from_floating = tier0_class_floating(from[signature]);
        const AbiLocation parameter = { .registers = { from_floating ? BUSTER_ABI_REGISTER_VECTOR : BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE },
                                        .kind = BUSTER_ABI_LOCATION_REGISTERS };
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = 3;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = 1;
        function.parameter_count = 1;
        function.parameters = &parameter;
        function.result = (AbiReturn){ .kind = BUSTER_ABI_RESULT_REGISTERS, .part_bytes = 8,
                                       .registers = { from_floating ? BUSTER_ABI_RAX : BUSTER_ABI_REGISTER_VECTOR, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE,
                                                      BUSTER_ABI_REGISTER_NONE } };
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        ready = tier0_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "unsigned floating",
                                  "%s: rule \"%s\" at %u, error %d, or the paths differ", names[signature], check.rule != 0 ? check.rule : "", check.node, scalar.error);
        ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.branch_count == 0, "unsigned floating",
                                           "%s: an invalid record or a branch", names[signature]);
        for (uint32_t encoder = 0; encoder < 2 && ready; encoder += 1)
        {
            const Tier0Code* code = encoder == 0 ? &vector : &scalar;
            uint8_t* page = region + (signature * 2 + encoder) * BUSTER_TIER0_TEST_PAGE;
            const int64_t length = encoder == 0 ? x86_encode(code->records, code->count, page, BUSTER_TIER0_TEST_PAGE, 0, 0)
                                                : x86_encode_scalar(code->records, code->count, page, BUSTER_TIER0_TEST_PAGE, 0, 0);
            ready = tier0_test_expect(tally, length > 0, "unsigned floating", "%s: encoder %u failed", names[signature], encoder);
        }
    }
    ready = ready && tier0_test_expect(tally, mprotect(region, 8 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) == 0, "unsigned floating", "the code can't be loaded");
    const pid_t child = ready ? fork() : -1;
    tier0_test_expect(tally, !ready || child >= 0, "unsigned floating", "fork failed");
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t run = 0; run < 8 * BUSTER_TIER0_TEST_UNSIGNED_MODES; run += 1)
        {
            void* address = region + run % 8 * BUSTER_TIER0_TEST_PAGE;
            uint64_t* block = shared + run / 8 * 16 * BUSTER_TIER0_TEST_UNSIGNED_VALUES;
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES; v += 1)
            {
                uint64_t bits = 0;
                _mm_setcsr((_mm_getcsr() & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_ROUND_MASK)) | (run / 8) << 13);
                if (run % 8 < 2)
                {
                    double (*convert)(uint64_t) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    const double d = convert(integers[v]);
                    memcpy(&bits, &d, sizeof(d));
                }
                else if (run % 8 < 4)
                {
                    float (*convert)(uint64_t) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    const float f = convert(integers[v]);
                    uint32_t low = 0;
                    memcpy(&low, &f, sizeof(f));
                    bits = low;
                }
                else if (run % 8 < 6)
                {
                    uint64_t (*convert)(double) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    bits = convert(doubles[v]);
                }
                else
                {
                    uint64_t (*convert)(float) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    bits = convert(floats[v]);
                }
                block[(8 + run % 8) * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v] = _mm_getcsr() & BUSTER_TIER0_TEST_UNSIGNED_FLAGS;
                block[run % 8 * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v] = bits;
            }
        }
        _exit(0);
    }
    if (child > 0)
    {
        int status = 0;
        waitpid(child, &status, 0);
        const bool ran = tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "unsigned floating", "the child's status %d", status);
        // Each call's bits and flags against C's conversion under the same rounding mode (the oracle's, compiled with
        // FENV_ACCESS), MXCSR put back after.
        static const char* const modes[BUSTER_TIER0_TEST_UNSIGNED_MODES] = { "to nearest", "down", "up", "toward zero" };
        const uint32_t csr = _mm_getcsr();
        for (uint32_t run = 0; run < 8 * BUSTER_TIER0_TEST_UNSIGNED_MODES && ran; run += 1)
        {
            const uint32_t signature = run % 8 / 2;
            const uint32_t mode = run / 8;
            const uint64_t* block = shared + mode * 16 * BUSTER_TIER0_TEST_UNSIGNED_VALUES;
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES; v += 1)
            {
                _mm_setcsr((csr & ~(uint32_t)_MM_ROUND_MASK) | mode << 13);
                uint64_t expected = 0;
                const uint32_t oracle = tier0_test_unsigned_oracle(signature, integers[v], doubles[v], floats[v], &expected);
                _mm_setcsr(csr);
                const uint64_t seen = block[run % 8 * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v];
                tier0_test_expect(tally, seen == expected, "unsigned floating", "%s, %s path, rounding %s: value %u gave 0x%016" PRIx64 ", 0x%016" PRIx64 " expected",
                                  names[signature], run % 2 == 0 ? "vector" : "scalar", modes[mode], v, seen, expected);
                const uint64_t flags = block[(8 + run % 8) * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v];
                tier0_test_expect(tally, flags == oracle, "unsigned floating", "%s, %s path, rounding %s: value %u raised flags 0x%02" PRIx64 ", C's conversion 0x%02x",
                                  names[signature], run % 2 == 0 ? "vector" : "scalar", modes[mode], v, flags, oracle);
            }
        }
    }
    // The conversions to U64 again, with invalid unmasked: every value is in U64's range, so none may trap.
    const pid_t unmasked = ready ? fork() : -1;
    tier0_test_expect(tally, !ready || unmasked >= 0, "unsigned floating", "fork failed");
    if (unmasked == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        _mm_setcsr(_mm_getcsr() & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_MASK_INVALID));
        for (uint32_t run = 4; run < 8; run += 1)
        {
            void* address = region + run * BUSTER_TIER0_TEST_PAGE;
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES; v += 1)
            {
                if (run < 6)
                {
                    uint64_t (*convert)(double) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    shared[v] = convert(doubles[v]);
                }
                else
                {
                    uint64_t (*convert)(float) = 0;
                    memcpy(&convert, &address, sizeof(convert));
                    shared[v] = convert(floats[v]);
                }
            }
        }
        _exit(0);
    }
    if (unmasked > 0)
    {
        int status = 0;
        waitpid(unmasked, &status, 0);
        tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "unsigned floating", "to U64 with invalid unmasked: the child's status %d%s", status,
                          WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE ? " (SIGFPE)" : "");
    }
    arena_release(&arena);
    if (region != MAP_FAILED)
    {
        munmap(region, 8 * BUSTER_TIER0_TEST_PAGE);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(uint64_t) * 16 * BUSTER_TIER0_TEST_UNSIGNED_VALUES * BUSTER_TIER0_TEST_UNSIGNED_MODES);
    }
}

// The conversions between U64 and the floating classes at the stacks' limits (#429's review): where the register form's
// scratch registers would be past their tables, the red zone form takes the conversion (tier0_u64_convert_registers), so
// that every depth with room for the conversion's value compiles. Each placement puts a conversion over integers and
// doubles held below it, with or without a frame pointer (STACK_SAVE's), at the last depth of each form or the one
// before it, as a function of x and out that converts x, stores the integers' sum at out[0] and the doubles' at out[1],
// and returns the conversion. Both paths must compile it alike, in the form the check's own rule gives (the red zone
// form's records are those at negative displacements from rsp), and each call, on the named values under each rounding
// mode, must give C's conversion, raise its flags and leave the values below as they were; and a conversion to U64 must
// run with invalid unmasked.
typedef struct Tier0TestLimit Tier0TestLimit;
struct Tier0TestLimit
{
    // Conversions to U64 (signatures 2 and 3) or from it (0 and 1); the integers below the conversion's own (`out` first,
    // then constants, and out + 8 last when there are doubles) and the doubles; and whether the function has a frame
    // pointer.
    bool to_u64;
    uint8_t integers;
    uint8_t doubles;
    bool pointer;
};

#define BUSTER_TIER0_TEST_LIMITS 12
#define BUSTER_TIER0_TEST_LIMIT_ENTRIES 96
// Each call's results: the conversion's bits, the flags it raised, then out[0] and out[1].
#define BUSTER_TIER0_TEST_LIMIT_RESULTS 4
#define BUSTER_TIER0_TEST_LIMIT_UNTOUCHED 0x5a5a5a5a5a5a5a5aull

// Integer k below a conversion (from 1: `out` is the first), and double j (from 0), whose sums are exact.
static uint64_t tier0_test_limit_integer(uint32_t k)
{
    return 0x9e3779b97f4a7c15ull * k;
}

static double tier0_test_limit_double(uint32_t j)
{
    return (double)j + 1.25;
}

static void tier0_test_limit_add(uint8_t* kinds, uint8_t* classes, uint64_t* values, uint32_t* count, uint8_t kind, uint8_t class, uint64_t value)
{
    kinds[*count] = kind;
    classes[*count] = class;
    values[*count] = value;
    *count += 1;
}

// The constants among a placement's integers: those but `out`, and out + 8 under its doubles.
static uint32_t tier0_test_limit_constants(const Tier0TestLimit* limit)
{
    return limit->integers - 1u - (limit->doubles != 0 ? 1u : 0u);
}

// Placement `limit`'s function for signature `signature` into the columns, which returns its entries' count. Its locals:
// 0 x, 1 out, 2 the conversion's result and 3 the stack's position. The stream's values are one stack in push order, so
// the doubles' sum is stored first, at the address under them; each sum is folded onto a zero pushed above its values, so
// that each entry's last operand is the entry before it.
static uint32_t tier0_test_limit_stream(const Tier0TestLimit* limit, uint32_t signature, uint8_t* kinds, uint8_t* classes, uint64_t* values)
{
    const uint8_t to = tier0_test_unsigned_to[signature];
    const uint8_t u64 = BUSTER_TYPED_CLASS_U64;
    const uint8_t f64 = BUSTER_TYPED_CLASS_F64;
    uint32_t count = 0;
    if (limit->pointer)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_STACK_SAVE, u64, 0);
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_SET_LOCAL, u64, 3);
    }
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, u64, 1);
    for (uint32_t k = 1; k <= tier0_test_limit_constants(limit); k += 1)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONST, u64, tier0_test_limit_integer(k));
    }
    if (limit->doubles != 0)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, u64, 1);
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONST, u64, 8);
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_ADD, u64, 0);
    }
    for (uint32_t j = 0; j < limit->doubles; j += 1)
    {
        const double value = tier0_test_limit_double(j);
        uint64_t bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONST, f64, bits);
    }
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, tier0_test_unsigned_from[signature], 0);
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONVERT, to, 0);
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_SET_LOCAL, to, 2);
    if (limit->doubles != 0)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONST, f64, 0);
        for (uint32_t j = 0; j < limit->doubles; j += 1)
        {
            tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_ADD, f64, 0);
        }
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_SET, f64, 0);
    }
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_CONST, u64, 0);
    for (uint32_t k = 1; k <= tier0_test_limit_constants(limit); k += 1)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_ADD, u64, 0);
    }
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_SET, u64, 0);
    if (limit->pointer)
    {
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, u64, 3);
        tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_STACK_RESTORE, u64, 0);
    }
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, to, 2);
    tier0_test_limit_add(kinds, classes, values, &count, BUSTER_TYPED_RETURN, to, 0);
    return count;
}

// Whether the code has a record at a negative displacement from rsp: a red zone temporary's.
static bool tier0_test_red_zone(const Tier0Code* code)
{
    bool found = false;
    for (uint32_t r = 0; r < code->count; r += 1)
    {
        const X86Instruction* record = &code->records[r];
        found |= record->mode == BUSTER_X86_MEMORY_BASE && record->rm == X86_REGISTER_RSP && record->displacement < 0;
    }
    return found;
}

// Whether the code pushes general register `reg`: the prologue's save of a callee-saved one.
static bool tier0_test_pushes(const Tier0Code* code, uint8_t reg)
{
    bool found = false;
    for (uint32_t r = 0; r < code->count; r += 1)
    {
        found |= code->records[r].form == X86_FORM_PUSH_R64 && code->records[r].rm == reg;
    }
    return found;
}

// Signature `signature`'s function at `address` called on value v, its out[0] and out[1] into out: the conversion's bits.
static uint64_t tier0_test_limit_call(void* address, uint32_t signature, uint32_t v, uint64_t* out)
{
    uint64_t bits = 0;
    if (signature == 0)
    {
        double (*convert)(uint64_t, uint64_t*) = 0;
        memcpy(&convert, &address, sizeof(convert));
        const double d = convert(tier0_test_unsigned_integers[v], out);
        memcpy(&bits, &d, sizeof(d));
    }
    else if (signature == 1)
    {
        float (*convert)(uint64_t, uint64_t*) = 0;
        memcpy(&convert, &address, sizeof(convert));
        const float f = convert(tier0_test_unsigned_integers[v], out);
        uint32_t low = 0;
        memcpy(&low, &f, sizeof(f));
        bits = low;
    }
    else if (signature == 2)
    {
        uint64_t (*convert)(double, uint64_t*) = 0;
        memcpy(&convert, &address, sizeof(convert));
        bits = convert(tier0_test_unsigned_doubles[v], out);
    }
    else
    {
        uint64_t (*convert)(float, uint64_t*) = 0;
        memcpy(&convert, &address, sizeof(convert));
        bits = convert(tier0_test_unsigned_floats[v], out);
    }
    return bits;
}

static void tier0_test_unsigned_limits(Tier0TestTally* tally)
{
    static const Tier0TestLimit limits[BUSTER_TIER0_TEST_LIMITS] = {
        // From U64: x in the integer table's last register but one, then in its last (14, or 13 with a frame pointer),
        // which leaves the register form no scratch; and that with the result in xmm15.
        { false, 13, 0, false },
        { false, 14, 0, false },
        { false, 12, 0, true },
        { false, 13, 0, true },
        { false, 14, 15, false },
        // To U64: the result in the integer table's last register but one, then in its last; x in xmm14, then in xmm15,
        // which leaves the register form no floating scratch; and the result in the last register with x in xmm15.
        { true, 13, 0, false },
        { true, 14, 0, false },
        { true, 12, 0, true },
        { true, 13, 0, true },
        { true, 2, 14, false },
        { true, 2, 15, false },
        { true, 14, 15, false },
    };
    static const char* const modes[BUSTER_TIER0_TEST_UNSIGNED_MODES] = { "to nearest", "down", "up", "toward zero" };
    enum
    {
        functions = BUSTER_TIER0_TEST_LIMITS * 2,
        pages = functions * 2,
    };
    const uint64_t calls = (uint64_t)pages * BUSTER_TIER0_TEST_UNSIGNED_MODES * BUSTER_TIER0_TEST_UNSIGNED_VALUES;
    uint64_t* shared = mmap(0, sizeof(uint64_t) * BUSTER_TIER0_TEST_LIMIT_RESULTS * calls, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* region = mmap(0, pages * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1u << 22);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && region != MAP_FAILED && arena.base != 0, "unsigned floating limits", "no memory");
    for (uint32_t f = 0; f < functions && ready; f += 1)
    {
        const Tier0TestLimit* limit = &limits[f / 2];
        const uint32_t signature = (limit->to_u64 ? 2u : 0u) + f % 2;
        uint8_t kinds[BUSTER_TIER0_TEST_LIMIT_ENTRIES];
        uint8_t classes[BUSTER_TIER0_TEST_LIMIT_ENTRIES];
        uint64_t values[BUSTER_TIER0_TEST_LIMIT_ENTRIES];
        const uint32_t count = tier0_test_limit_stream(limit, signature, kinds, classes, values);
        const uint8_t local_classes[4] = { tier0_test_unsigned_from[signature], BUSTER_TYPED_CLASS_U64, tier0_test_unsigned_to[signature], BUSTER_TYPED_CLASS_U64 };
        const uint8_t local_flags[4] = { 0, 0, 0, 0 };
        const bool from_floating = tier0_class_floating(tier0_test_unsigned_from[signature]);
        const AbiLocation parameters[2] = {
            { .registers = { from_floating ? BUSTER_ABI_REGISTER_VECTOR : BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
            { .registers = { from_floating ? BUSTER_ABI_RDI : BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        };
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = 4;
        function.parameter_count = 2;
        function.parameters = parameters;
        function.result = (AbiReturn){ .kind = BUSTER_ABI_RESULT_REGISTERS, .part_bytes = 8,
                                       .registers = { from_floating ? BUSTER_ABI_RAX : BUSTER_ABI_REGISTER_VECTOR, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE,
                                                      BUSTER_ABI_REGISTER_NONE } };
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        ready = tier0_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "unsigned floating limits",
                                  "%s over %u integers and %u doubles, %s frame pointer: rule \"%s\" at %u, error %d at %u, or the paths differ",
                                  tier0_test_unsigned_names[signature], limit->integers, limit->doubles, limit->pointer ? "a" : "no", check.rule != 0 ? check.rule : "",
                                  check.node, scalar.error, scalar.node);
        ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.branch_count == 0, "unsigned floating limits",
                                           "%s over %u integers and %u doubles: an invalid record or a branch", tier0_test_unsigned_names[signature], limit->integers,
                                           limit->doubles);
        // The form, by the integer stack's depth at the conversion (x's among them, from U64) and its table's depths.
        const uint32_t depths = limit->pointer ? 14 : 15;
        const uint32_t depth = limit->integers + (limit->to_u64 ? 0u : 1u);
        const bool registers = limit->to_u64 ? depth + 1 < depths && limit->doubles + 1u < 16 : depth < depths;
        ready = ready && tier0_test_expect(tally, tier0_test_red_zone(&scalar) == !registers, "unsigned floating limits",
                                           "%s over %u integers and %u doubles, %s frame pointer: not the %s form", tier0_test_unsigned_names[signature],
                                           limit->integers, limit->doubles, limit->pointer ? "a" : "no", registers ? "register" : "red zone");
        // The register form's integer scratch, a callee-saved register past the caller-saved depths, which the selections'
        // tops must count, so that the prologue saves it.
        const uint32_t scratch = limit->to_u64 ? depth + 1 : depth;
        const bool saved = !registers || scratch < BUSTER_TIER0_CALLER_SAVED_DEPTHS || tier0_test_pushes(&scalar, tier0_integer_depths[scratch]);
        ready = ready && tier0_test_expect(tally, saved, "unsigned floating limits", "%s over %u integers and %u doubles, %s frame pointer: depth %u's register unsaved",
                                           tier0_test_unsigned_names[signature], limit->integers, limit->doubles, limit->pointer ? "a" : "no", scratch);
        for (uint32_t encoder = 0; encoder < 2 && ready; encoder += 1)
        {
            const Tier0Code* code = encoder == 0 ? &vector : &scalar;
            uint8_t* page = region + (f * 2 + encoder) * BUSTER_TIER0_TEST_PAGE;
            const int64_t length = encoder == 0 ? x86_encode(code->records, code->count, page, BUSTER_TIER0_TEST_PAGE, 0, 0)
                                                : x86_encode_scalar(code->records, code->count, page, BUSTER_TIER0_TEST_PAGE, 0, 0);
            ready = tier0_test_expect(tally, length > 0, "unsigned floating limits", "%s over %u integers: encoder %u failed", tier0_test_unsigned_names[signature],
                                      limit->integers, encoder);
        }
    }
    ready = ready && tier0_test_expect(tally, mprotect(region, pages * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) == 0, "unsigned floating limits",
                                       "the code can't be loaded");
    const pid_t child = ready ? fork() : -1;
    tier0_test_expect(tally, !ready || child >= 0, "unsigned floating limits", "fork failed");
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t page = 0; page < pages; page += 1)
        {
            const Tier0TestLimit* limit = &limits[page / 4];
            const uint32_t signature = (limit->to_u64 ? 2u : 0u) + page / 2 % 2;
            for (uint32_t mode = 0; mode < BUSTER_TIER0_TEST_UNSIGNED_MODES; mode += 1)
            {
                for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES; v += 1)
                {
                    uint64_t out[2] = { BUSTER_TIER0_TEST_LIMIT_UNTOUCHED, BUSTER_TIER0_TEST_LIMIT_UNTOUCHED };
                    _mm_setcsr((_mm_getcsr() & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_ROUND_MASK)) | mode << 13);
                    const uint64_t bits = tier0_test_limit_call(region + page * BUSTER_TIER0_TEST_PAGE, signature, v, out);
                    uint64_t* results = shared + BUSTER_TIER0_TEST_LIMIT_RESULTS * (((uint64_t)page * BUSTER_TIER0_TEST_UNSIGNED_MODES + mode) * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v);
                    results[1] = _mm_getcsr() & BUSTER_TIER0_TEST_UNSIGNED_FLAGS;
                    results[0] = bits;
                    results[2] = out[0];
                    results[3] = out[1];
                }
            }
        }
        _exit(0);
    }
    if (child > 0)
    {
        int status = 0;
        waitpid(child, &status, 0);
        const bool ran = tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "unsigned floating limits", "the child's status %d", status);
        const uint32_t csr = _mm_getcsr();
        for (uint32_t page = 0; page < pages && ran; page += 1)
        {
            const Tier0TestLimit* limit = &limits[page / 4];
            const uint32_t signature = (limit->to_u64 ? 2u : 0u) + page / 2 % 2;
            // The values below: the integers' sum (wrapping) and the doubles', or out[1] untouched without doubles.
            uint64_t integers = 0;
            for (uint32_t k = 1; k <= tier0_test_limit_constants(limit); k += 1)
            {
                integers += tier0_test_limit_integer(k);
            }
            double sum = 0.0;
            for (uint32_t j = limit->doubles; j-- > 0;)
            {
                sum = tier0_test_limit_double(j) + sum;
            }
            uint64_t doubles = BUSTER_TIER0_TEST_LIMIT_UNTOUCHED;
            if (limit->doubles != 0)
            {
                memcpy(&doubles, &sum, sizeof(doubles));
            }
            for (uint32_t mode = 0; mode < BUSTER_TIER0_TEST_UNSIGNED_MODES; mode += 1)
            {
                for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES; v += 1)
                {
                    _mm_setcsr((csr & ~(uint32_t)_MM_ROUND_MASK) | mode << 13);
                    uint64_t expected = 0;
                    const uint32_t oracle = tier0_test_unsigned_oracle(signature, tier0_test_unsigned_integers[v], tier0_test_unsigned_doubles[v],
                                                                       tier0_test_unsigned_floats[v], &expected);
                    _mm_setcsr(csr);
                    const uint64_t* results = shared + BUSTER_TIER0_TEST_LIMIT_RESULTS * (((uint64_t)page * BUSTER_TIER0_TEST_UNSIGNED_MODES + mode) * BUSTER_TIER0_TEST_UNSIGNED_VALUES + v);
                    tier0_test_expect(tally, results[0] == expected && results[1] == oracle && results[2] == integers && results[3] == doubles, "unsigned floating limits",
                                      "%s over %u integers and %u doubles, %s frame pointer, %s path, rounding %s, value %u: 0x%016" PRIx64 " with flags 0x%02" PRIx64
                                      " and out 0x%016" PRIx64 " 0x%016" PRIx64 ", C's 0x%016" PRIx64 " with 0x%02x and 0x%016" PRIx64 " 0x%016" PRIx64,
                                      tier0_test_unsigned_names[signature], limit->integers, limit->doubles, limit->pointer ? "a" : "no", page % 2 == 0 ? "vector" : "scalar",
                                      modes[mode], v, results[0], results[1], results[2], results[3], expected, oracle, integers, doubles);
                }
            }
        }
    }
    // The conversions to U64 again, with invalid unmasked: every value is in U64's range, so none may trap.
    const pid_t unmasked = ready ? fork() : -1;
    tier0_test_expect(tally, !ready || unmasked >= 0, "unsigned floating limits", "fork failed");
    if (unmasked == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        _mm_setcsr(_mm_getcsr() & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_MASK_INVALID));
        for (uint32_t page = 0; page < pages; page += 1)
        {
            const Tier0TestLimit* limit = &limits[page / 4];
            const uint32_t signature = (limit->to_u64 ? 2u : 0u) + page / 2 % 2;
            for (uint32_t v = 0; v < BUSTER_TIER0_TEST_UNSIGNED_VALUES && limit->to_u64; v += 1)
            {
                uint64_t out[2] = { 0, 0 };
                shared[v] = tier0_test_limit_call(region + page * BUSTER_TIER0_TEST_PAGE, signature, v, out);
            }
        }
        _exit(0);
    }
    if (unmasked > 0)
    {
        int status = 0;
        waitpid(unmasked, &status, 0);
        tier0_test_expect(tally, WIFEXITED(status) && WEXITSTATUS(status) == 0, "unsigned floating limits", "to U64 with invalid unmasked: the child's status %d%s",
                          status, WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE ? " (SIGFPE)" : "");
    }
    arena_release(&arena);
    if (region != MAP_FAILED)
    {
        munmap(region, pages * BUSTER_TIER0_TEST_PAGE);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(uint64_t) * BUSTER_TIER0_TEST_LIMIT_RESULTS * calls);
    }
}

// An entry's last operand must have the class of the entry before it, which both paths read for it (in postorder,
// that entry is the operand), so that tier0_validate must refuse each stream below whose operand has another class,
// at that entry, and take the others: the same in postorder, a store that pushes before its consumer, DUP, values
// left under a store of their class (an increment's old value, as #357's producer leaves it), and calls, which read
// their own arguments' classes. Locals: 0 an I64, 1 a U64 (an indirect call's callee), 2 and 3 objects of 16 bytes;
// call 0 is direct, of one I64, and call 1 indirect, of none.
typedef struct Tier0TestEntry Tier0TestEntry;
struct Tier0TestEntry
{
    uint8_t kind;
    uint8_t class;
    uint64_t value;
};

typedef struct Tier0TestOperands Tier0TestOperands;
struct Tier0TestOperands
{
    const char* name;
    // The entry tier0_validate must refuse, or UINT32_MAX for a stream in postorder.
    uint32_t broken;
    uint32_t count;
    Tier0TestEntry entries[10];
};

#define BUSTER_TIER0_TEST_ONE 0x3ff0000000000000ull
#define BUSTER_TIER0_TEST_TWO 0x4000000000000000ull

static const Tier0TestOperands tier0_test_operand_fixtures[] = {
    { "a binary operation after a SET_LOCAL of another class", 4, 7, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_TWO },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the binary operation in postorder", UINT32_MAX, 7, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_TWO },
        { BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a compare after a SET_LOCAL of another class", 4, 7, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_TWO },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a DISCARD after a COPY", 4, 6, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 5 },
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3 },
        { BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the DISCARD in postorder", UINT32_MAX, 6, {
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3 },
        { BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 5 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a CONVERT after a SET of another class", 4, 7, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3 },
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 9 },
        { BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the CONVERT in postorder", UINT32_MAX, 7, {
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 9 },
        { BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 3 },
        { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a SET's value under a SET_LOCAL of its class", UINT32_MAX, 6, {
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 9 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a COPY's source after a SET_LOCAL of another class", 4, 6, {
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2 },
        { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a call's last argument under a SET_LOCAL", UINT32_MAX, 6, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 4 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the call in postorder", UINT32_MAX, 6, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 4 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "an indirect call's callee under a SET_LOCAL", UINT32_MAX, 6, {
        { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the indirect call in postorder", UINT32_MAX, 6, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a returned value under a SET_LOCAL of its class", UINT32_MAX, 4, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0 } } },
    { "a returned value after a SET_LOCAL of another class", 3, 4, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F64, 0 } } },
    { "a returned value from under a call of nothing", 3, 4, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 5 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 4 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0 } } },
    { "the value from under the call in postorder", UINT32_MAX, 6, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 5 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 4 },
        { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0 } } },
    { "the returned value in postorder", UINT32_MAX, 4, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0 } } },
    { "a branch's condition after a SET_LOCAL of another class", 3, 5, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the branch in postorder", UINT32_MAX, 5, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "an arm's value after a SET_LOCAL of another class", 5, 9, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 6 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_F64, 7 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_TWO },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "the arm in postorder", UINT32_MAX, 9, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 6 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7 },
        { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_ONE },
        { BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_F64, 7 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F64, BUSTER_TIER0_TEST_TWO },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "a store that pushes before its consumer", UINT32_MAX, 4, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "DUP before its consumer", UINT32_MAX, 5, {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1 },
        { BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
    { "an increment's old value under a SET_GLOBAL of its class", UINT32_MAX, 7, {
        { BUSTER_TYPED_GLOBAL, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_I32, 0 },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I32, 0 },
        { BUSTER_TYPED_SET_GLOBAL, BUSTER_TYPED_CLASS_I32, 1 },
        { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0 },
        { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } } },
};

static void tier0_test_operands(Tier0TestTally* tally)
{
    static const uint8_t local_classes[4] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID };
    static const uint8_t local_flags[4] = { 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    static const uint32_t local_sizes[4] = { 0, 0, 16, 16 };
    static const uint8_t local_alignments[4] = { 0, 0, 3, 3 };
    static const SymbolId targets[2] = { 1, 0 };
    static const uint32_t call_first[2] = { 0, 1 };
    static const uint32_t call_counts[2] = { 1, 0 };
    static const uint8_t call_variadic[2] = { 0, 0 };
    static const uint8_t argument_classes[1] = { BUSTER_TYPED_CLASS_I64 };
    static const AbiCall calls[2];
    static const AbiLocation argument_locations[1];
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(tier0_test_operand_fixtures); c += 1)
    {
        const Tier0TestOperands* fixture = &tier0_test_operand_fixtures[c];
        uint8_t kinds[10];
        uint8_t classes[10];
        uint64_t values[10];
        for (uint32_t e = 0; e < fixture->count; e += 1)
        {
            kinds[e] = fixture->entries[e].kind;
            classes[e] = fixture->entries[e].class;
            values[e] = fixture->entries[e].value;
        }
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = fixture->count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_sizes = local_sizes;
        function.local_alignments = local_alignments;
        function.local_count = 4;
        function.call_targets = targets;
        function.call_first = call_first;
        function.call_argument_counts = call_counts;
        function.call_variadic = call_variadic;
        function.calls = calls;
        function.argument_locations = argument_locations;
        function.argument_classes = argument_classes;
        function.call_count = 2;
        function.argument_count = 1;
        const Tier0Check check = tier0_validate(&function);
        const bool broken = fixture->broken != UINT32_MAX;
        const bool refused = check.error != TIER0_OK && check.node == fixture->broken && check.rule != 0 && strstr(check.rule, "the entry before it") != 0;
        tier0_test_expect(tally, broken ? refused : check.error == TIER0_OK, "operands", "%s: tier0_validate says %s at %u (%s)", fixture->name,
                          check.error == TIER0_OK ? "valid" : "invalid", check.node, check.rule != 0 ? check.rule : "");
    }
}

// The access column (Tier0Function.symbol_access). tier0_validate must refuse a global entry whose symbol is past the
// column or whose access doesn't exist, and take the column otherwise. Then each global kind of every class, by each
// access: GLOBAL, STORE_GLOBAL and SET_GLOBAL of a local's value, SET_GLOBAL of a constant, and GLOBAL_ADDRESS, whose
// symbol is 1 of 2. Both paths must compile it alike to valid records with one reference to symbol 1, of the access's
// kind (PC32, LOAD_GOT, TPOFF32 or GOTTPOFF), in the displacement but for a thread-local's address of the image's (the
// TPOFF32 immediate an add takes), with an FS override exactly for a thread-local, and for a direct symbol the code
// without the column. A constant stored into a symbol is an immediate (its CONST absorbed) only for a direct symbol,
// which a store of an integer's constant's record count shows.
static void tier0_test_access(Tier0TestTally* tally)
{
    static const uint8_t reference_kinds[BUSTER_TIER0_ACCESSES] = {
        BUSTER_LINK_FIXUP_PC32,
        BUSTER_LINK_FIXUP_LOAD_GOT,
        BUSTER_LINK_FIXUP_TPOFF32,
        BUSTER_LINK_FIXUP_GOTTPOFF,
    };
    // A store of an integer's constant, in records: the store and ret, with the constant's mov but directly, and the
    // slot's load through the GOT.
    static const uint32_t constant_records[BUSTER_TIER0_ACCESSES] = { 2, 4, 3, 4 };
    Arena arena = arena_reserve(1ull << 28);
    uint8_t column[2] = { BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_DIRECT };
    uint8_t local_classes[1] = { 0 };
    static const uint8_t local_flags[1] = { 0 };
    // Validation: a GLOBAL of symbol 1 with a column of 1 symbol, and with an access past the last.
    {
        static const uint8_t kinds[2] = { BUSTER_TYPED_GLOBAL, BUSTER_TYPED_RETURN };
        static const uint8_t classes[2] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32 };
        static const uint64_t values[2] = { 1, 0 };
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = 2;
        function.symbol_access = column;
        function.symbol_count = 1;
        const Tier0Check short_column = tier0_validate(&function);
        function.symbol_count = 2;
        column[1] = BUSTER_TIER0_ACCESSES;
        const Tier0Check past = tier0_validate(&function);
        column[1] = BUSTER_TIER0_ACCESS_INITIAL_EXEC;
        const Tier0Check last = tier0_validate(&function);
        tier0_test_expect(tally, short_column.error == TIER0_ERROR_INVALID && short_column.node == 0 && past.error == TIER0_ERROR_INVALID && past.node == 0 &&
                                     last.error == TIER0_OK,
                          "access", "tier0_validate: a symbol past the column %d at %u, an access past the last %d at %u, the last %d", short_column.error,
                          short_column.node, past.error, past.node, last.error);
    }
    // Every class a global holds as a value (a long double's is read and written through its address, which the x87 check
    // covers).
    for (uint32_t class = BUSTER_TYPED_CLASS_I8; class <= BUSTER_TYPED_CLASS_F64; class += 1)
    {
        for (uint32_t shape = 0; shape < 5; shape += 1)
        {
            // 0: GLOBAL; 1: STORE_GLOBAL of a local; 2: SET_GLOBAL of a local; 3: SET_GLOBAL of a constant; 4: GLOBAL_ADDRESS
            // (of a U64 alone).
            if (shape == 4 && class != BUSTER_TYPED_CLASS_U64)
            {
                continue;
            }
            uint8_t kinds[4];
            uint8_t classes[4];
            uint64_t values[4] = { 0, 1, 0, 0 };
            uint32_t count = 0;
            local_classes[0] = (uint8_t)class;
            if (shape == 0 || shape == 4)
            {
                kinds[0] = shape == 0 ? BUSTER_TYPED_GLOBAL : BUSTER_TYPED_GLOBAL_ADDRESS;
                values[0] = 1;
                kinds[1] = BUSTER_TYPED_RETURN;
                classes[0] = (uint8_t)class;
                classes[1] = (uint8_t)class;
                count = 2;
            }
            else
            {
                kinds[0] = shape == 3 ? BUSTER_TYPED_CONST : BUSTER_TYPED_LOCAL;
                values[0] = shape == 3 ? (class == BUSTER_TYPED_CLASS_F64 ? 0x4000000000000000ull : class == BUSTER_TYPED_CLASS_F32 ? 0x40000000ull : 5) : 0;
                kinds[1] = shape == 1 ? BUSTER_TYPED_STORE_GLOBAL : BUSTER_TYPED_SET_GLOBAL;
                kinds[2] = shape == 1 ? BUSTER_TYPED_DISCARD : BUSTER_TYPED_RETURN;
                kinds[3] = BUSTER_TYPED_RETURN;
                classes[0] = (uint8_t)class;
                classes[1] = (uint8_t)class;
                classes[2] = shape == 1 ? (uint8_t)class : BUSTER_TYPED_CLASS_VOID;
                classes[3] = BUSTER_TYPED_CLASS_VOID;
                count = shape == 1 ? 4 : 3;
            }
            Tier0Function function;
            memset(&function, 0, sizeof(function));
            function.kinds = kinds;
            function.classes = classes;
            function.values = values;
            function.count = count;
            function.local_classes = local_classes;
            function.local_flags = local_flags;
            function.local_count = 1;
            const uint64_t mark = arena.length;
            const Tier0Code plain = tier0_compile_scalar(&function, &arena);
            function.symbol_access = column;
            function.symbol_count = 2;
            for (uint32_t access = 0; access < BUSTER_TIER0_ACCESSES; access += 1)
            {
                column[1] = (uint8_t)access;
                const Tier0Check check = tier0_validate(&function);
                const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
                const Tier0Code vector = tier0_compile(&function, &arena);
                const bool compiled = check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector) &&
                                      x86_validate(scalar.records, scalar.count) == scalar.count;
                bool segment = false;
                for (uint32_t r = 0; compiled && r < scalar.count; r += 1)
                {
                    segment |= (scalar.records[r].form & BUSTER_X86_FORM_NUMBER) >= BUSTER_X86_FORM_FS;
                }
                const bool immediate = shape == 4 && access == BUSTER_TIER0_ACCESS_LOCAL_EXEC;
                const bool referenced = compiled && scalar.reference_count == 1 && scalar.references[0].kind == reference_kinds[access] &&
                                        scalar.references[0].target == 1 && scalar.references[0].field == (immediate ? X86_FIXUP_IMMEDIATE : X86_FIXUP_DISPLACEMENT);
                const bool thread_local = access == BUSTER_TIER0_ACCESS_LOCAL_EXEC || access == BUSTER_TIER0_ACCESS_INITIAL_EXEC;
                const bool direct = access != BUSTER_TIER0_ACCESS_DIRECT || tier0_test_same_code(&plain, &scalar);
                const bool constant = shape != 3 || tier0_class_floating(class) || scalar.count == constant_records[access];
                tier0_test_expect(tally, compiled && referenced && segment == thread_local && direct && constant, "access",
                                  "class %u, shape %u, access %u: validated %d, compiled %d, the paths alike %d, %u records, %u references (kind %u, field %u), "
                                  "FS %d, the direct code the code without the column %d",
                                  class, shape, access, check.error, scalar.error, tier0_test_same_code(&scalar, &vector), scalar.count, scalar.reference_count,
                                  scalar.reference_count != 0 ? scalar.references[0].kind : 0, scalar.reference_count != 0 ? scalar.references[0].field : 0, segment,
                                  direct);
            }
            arena_pop_to(&arena, mark);
        }
    }
    arena_release(&arena);
}

// ---------------------------------------------------------------------------------------------------------------------
// Aggregates at hand-made locations, run natively

// What abi_assign makes rarely or never on its own, and the edges of the parts (#137): arguments and parameters (an
// aggregate of `size` bytes aligned to 2^alignment_log2, or for size 0 a value of `class`) at locations written by
// hand, and results. Each fixture runs both ways: as a call, Tier 0's caller passing its aggregates' bytes from pages
// that end or start at a PROT_NONE page to a probe that records the registers and the stack slots it gets
// (tier0_test_aggregate_calls); and as a function, Tier 0's, whose parameters a driver passes from the same bytes and
// which copies them out (tier0_test_aggregate_functions). tier0_test_aggregate_errors breaks them for tier0_validate.
typedef struct Tier0TestPassed Tier0TestPassed;
struct Tier0TestPassed
{
    uint32_t size;
    uint8_t class;
    uint8_t alignment_log2;
    AbiLocation location;
};

#define BUSTER_TIER0_TEST_PASSED 10

typedef struct Tier0TestPassing Tier0TestPassing;
struct Tier0TestPassing
{
    const char* name;
    uint32_t count;
    uint32_t stack_bytes;
    Tier0TestPassed passed[BUSTER_TIER0_TEST_PASSED];
    AbiReturn result;
    uint32_t result_size;
};

#define BUSTER_TIER0_TEST_N BUSTER_ABI_REGISTER_NONE
#define BUSTER_TIER0_TEST_X(n) (BUSTER_ABI_REGISTER_VECTOR | (n))
#define BUSTER_TIER0_TEST_HIGH BUSTER_ABI_FLAG_HIGH
#define BUSTER_TIER0_TEST_LOW BUSTER_ABI_FLAG_LOW
// An aggregate of `bytes` aligned to 2^`log2`, in registers `first` and `second` and at `offset` on the stack; a value
// of `class` in register `first` or at `offset`; a result in registers `first` and `second`.
#define BUSTER_TIER0_TEST_AGGREGATE(bytes, log2, kind, flags, first, second, offset)                                     \
    { (bytes), BUSTER_TYPED_CLASS_U64, (log2), { (offset), { (first), (second) }, BUSTER_ABI_LOCATION_##kind, (flags) } }
#define BUSTER_TIER0_TEST_VALUE(class, kind, first, offset)                                                             \
    { 0, BUSTER_TYPED_CLASS_##class, 0, { (offset), { (first), BUSTER_TIER0_TEST_N }, BUSTER_ABI_LOCATION_##kind, 0 } }
#define BUSTER_TIER0_TEST_RESULT(kind, flags, part_bytes, first, second)                                                \
    { BUSTER_ABI_RESULT_##kind, (flags), (part_bytes), { (first), (second), BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N }, 0 }
#define BUSTER_TIER0_TEST_NO_RESULT BUSTER_TIER0_TEST_RESULT(IGNORE, 0, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N)

static const Tier0TestPassing tier0_test_passings[] = {
    // Each kind of general part, and aggregates on the stack whole: a 24-byte one aligned to 16 in a slot at 56, which
    // a callee copies into its own object, beside one in place at 16.
    { "general parts", 9, 96, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 3, REGISTERS, 0, BUSTER_ABI_RDI, BUSTER_ABI_RSI, 0),
        BUSTER_TIER0_TEST_AGGREGATE(11, 0, REGISTERS, 0, BUSTER_ABI_RDX, BUSTER_ABI_RCX, 0),
        BUSTER_TIER0_TEST_AGGREGATE(3, 0, REGISTERS, 0, BUSTER_ABI_R8, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(6, 1, REGISTERS, 0, BUSTER_ABI_R9, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(13, 0, STACK, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(40, 4, STACK, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 16),
        BUSTER_TIER0_TEST_AGGREGATE(24, 4, STACK, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 56),
        BUSTER_TIER0_TEST_AGGREGATE(7, 0, STACK, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 80),
        BUSTER_TIER0_TEST_VALUE(I64, STACK, BUSTER_TIER0_TEST_N, 88),
    }, BUSTER_TIER0_TEST_NO_RESULT, 0 },
    // Each kind of vector part, and parts of both kinds in one aggregate.
    { "vector parts", 9, 0, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 4, REGISTERS, 0, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(12, 2, REGISTERS, 0, BUSTER_TIER0_TEST_X(1), BUSTER_TIER0_TEST_X(2), 0),
        BUSTER_TIER0_TEST_AGGREGATE(6, 1, REGISTERS, 0, BUSTER_TIER0_TEST_X(3), BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(14, 1, REGISTERS, 0, BUSTER_TIER0_TEST_X(4), BUSTER_ABI_RDI, 0),
        BUSTER_TIER0_TEST_AGGREGATE(10, 1, REGISTERS, 0, BUSTER_ABI_RSI, BUSTER_TIER0_TEST_X(5), 0),
        BUSTER_TIER0_TEST_AGGREGATE(2, 1, REGISTERS, 0, BUSTER_TIER0_TEST_X(6), BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_VALUE(F64, REGISTERS, BUSTER_TIER0_TEST_X(7), 0),
        BUSTER_TIER0_TEST_VALUE(I64, REGISTERS, BUSTER_ABI_RDX, 0),
        BUSTER_TIER0_TEST_AGGREGATE(5, 0, REGISTERS, 0, BUSTER_ABI_RCX, BUSTER_TIER0_TEST_N, 0),
    }, BUSTER_TIER0_TEST_NO_RESULT, 0 },
    // A high eightbyte alone in each kind of register and on the stack, SPLIT both ways in both kinds, and an aggregate
    // the call ignores.
    { "high, split and ignored", 10, 40, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 3, REGISTERS, BUSTER_TIER0_TEST_HIGH, BUSTER_ABI_RDI, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(11, 0, REGISTERS, BUSTER_TIER0_TEST_HIGH, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(13, 0, REGISTERS, BUSTER_TIER0_TEST_HIGH, BUSTER_ABI_RSI, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(12, 2, SPLIT, 0, BUSTER_ABI_RDX, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(15, 0, SPLIT, 0, BUSTER_TIER0_TEST_N, BUSTER_ABI_RCX, 8),
        BUSTER_TIER0_TEST_AGGREGATE(24, 3, IGNORE, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_AGGREGATE(12, 2, STACK, BUSTER_TIER0_TEST_HIGH, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 16),
        BUSTER_TIER0_TEST_VALUE(I64, REGISTERS, BUSTER_ABI_R8, 0),
        BUSTER_TIER0_TEST_AGGREGATE(9, 0, SPLIT, 0, BUSTER_TIER0_TEST_X(1), BUSTER_TIER0_TEST_N, 24),
        BUSTER_TIER0_TEST_AGGREGATE(10, 1, SPLIT, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_X(2), 32),
    }, BUSTER_TIER0_TEST_NO_RESULT, 0 },
    // Results, in each kind of register and both orders, a high eightbyte alone, parts that load through a temporary,
    // and through a hidden pointer.
    { "a result in rax and rdx", 1, 0, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 3, REGISTERS, 0, BUSTER_ABI_RDI, BUSTER_ABI_RSI, 0),
    }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_ABI_RAX, BUSTER_ABI_RDX), 16 },
    { "a result in xmm0 and xmm1", 1, 0, {
        BUSTER_TIER0_TEST_AGGREGATE(12, 2, REGISTERS, 0, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_X(1), 0),
    }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_X(1)), 12 },
    { "a result in rax and xmm0", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_ABI_RAX, BUSTER_TIER0_TEST_X(0)), 16 },
    { "a result in xmm0 and rax", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_TIER0_TEST_X(0), BUSTER_ABI_RAX), 13 },
    { "a result's high eightbyte in rax", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, BUSTER_TIER0_TEST_HIGH, 8, BUSTER_ABI_RAX, BUSTER_TIER0_TEST_N), 16 },
    { "a result's high eightbyte in xmm0", 0, 0, { { 0 } },
      BUSTER_TIER0_TEST_RESULT(REGISTERS, BUSTER_TIER0_TEST_HIGH, 8, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_N), 11 },
    { "a vector result", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 16, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_N), 16 },
    { "a result of 5 bytes in rax", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_ABI_RAX, BUSTER_TIER0_TEST_N), 5 },
    { "a result of 6 bytes in xmm0", 0, 0, { { 0 } }, BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_TIER0_TEST_X(0), BUSTER_TIER0_TEST_N), 6 },
    { "a hidden result", 2, 0, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 3, REGISTERS, 0, BUSTER_ABI_RSI, BUSTER_ABI_RDX, 0),
        BUSTER_TIER0_TEST_VALUE(I64, REGISTERS, BUSTER_ABI_RCX, 0),
    }, BUSTER_TIER0_TEST_RESULT(MEMORY, 0, 0, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N), 40 },
    // The low eightbyte alone of a larger aggregate in its 8-byte slot (LOW: where clang's count has a register LLVM
    // hasn't), each before a value in the next slot, which a copy of the aggregate's bytes would overwrite.
    { "low eightbytes on the stack", 4, 32, {
        BUSTER_TIER0_TEST_AGGREGATE(16, 3, STACK, BUSTER_TIER0_TEST_LOW, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 0),
        BUSTER_TIER0_TEST_VALUE(I64, STACK, BUSTER_TIER0_TEST_N, 8),
        BUSTER_TIER0_TEST_AGGREGATE(10, 1, STACK, BUSTER_TIER0_TEST_LOW, BUSTER_TIER0_TEST_N, BUSTER_TIER0_TEST_N, 16),
        BUSTER_TIER0_TEST_VALUE(I64, STACK, BUSTER_TIER0_TEST_N, 24),
    }, BUSTER_TIER0_TEST_NO_RESULT, 0 },
};

// The words of the stack a probe records and a driver passes, a hidden result's bytes, each parameter's place in the out
// bytes a function copies them to, and where in it an aggregate's address goes, after its bytes.
#define BUSTER_TIER0_TEST_STACK_WORDS 16
#define BUSTER_TIER0_TEST_HIDDEN_BYTES 40
#define BUSTER_TIER0_TEST_SLOT_BYTES 48
#define BUSTER_TIER0_TEST_ADDRESS_AT 40
// Two placements of the bytes (ending at a PROT_NONE page, and starting after one) by each encoder.
#define BUSTER_TIER0_TEST_AGGREGATE_RUNS 4

// What a probe saw, or a driver's call returned (rax, rdx, xmm0 and xmm1), and the bytes a caller copied its result
// object to or a function its parameters: the run in progress's (runs[0]), copied to its own after it.
typedef struct Tier0TestSeen Tier0TestSeen;
struct Tier0TestSeen
{
    uint64_t general[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint64_t rax;
    uint64_t rdx;
    uint8_t vectors[BUSTER_ABI_VECTOR_ARGUMENTS][16];
    uint8_t stack[8 * BUSTER_TIER0_TEST_STACK_WORDS];
    uint8_t out[BUSTER_TIER0_TEST_PASSED * BUSTER_TIER0_TEST_SLOT_BYTES];
    uint8_t hidden[BUSTER_TIER0_TEST_HIDDEN_BYTES];
};

// What a probe returns (rax, rdx, xmm0 and xmm1, and with hidden_flag the bytes it writes where rdi points, or else
// into the sink), and what a driver passes (the argument registers and the stack's slots).
typedef struct Tier0TestPreset Tier0TestPreset;
struct Tier0TestPreset
{
    uint64_t general[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint64_t rax;
    uint64_t rdx;
    uint8_t vectors[BUSTER_ABI_VECTOR_ARGUMENTS][16];
    uint8_t stack[8 * BUSTER_TIER0_TEST_STACK_WORDS];
    uint64_t hidden_flag;
    uint8_t hidden[BUSTER_TIER0_TEST_HIDDEN_BYTES];
    uint8_t sink[BUSTER_TIER0_TEST_HIDDEN_BYTES];
};

typedef struct Tier0TestShared Tier0TestShared;
struct Tier0TestShared
{
    Tier0TestPreset preset;
    Tier0TestSeen runs[1 + BUSTER_TIER0_TEST_AGGREGATE_RUNS];
};

// The bytes of an argument register (Tier 0's numbering: rdi to r9, xmm0 to xmm7), or with `result` of a result
// register (rax, rdx, xmm0 and xmm1), as a probe recorded or a driver passes them; 0 for another.
static uint8_t* tier0_test_register_bytes(uint64_t* general, uint64_t* rax, uint64_t* rdx, uint8_t (*vectors)[16], uint8_t reg, bool result)
{
    uint8_t* bytes = reg >= 16 && reg < 16 + BUSTER_ABI_VECTOR_ARGUMENTS ? vectors[reg - 16] : 0;
    for (uint32_t g = 0; g < BUSTER_ABI_INTEGER_ARGUMENTS; g += 1)
    {
        bytes = !result && abi_integer_arguments[g] == reg ? (uint8_t*)&general[g] : bytes;
    }
    bytes = result && reg == X86_REGISTER_RAX ? (uint8_t*)rax : result && reg == X86_REGISTER_RDX ? (uint8_t*)rdx : bytes;
    return bytes;
}

static const uint8_t* tier0_test_seen_register(Tier0TestSeen* seen, uint8_t reg, bool result)
{
    return tier0_test_register_bytes(seen->general, &seen->rax, &seen->rdx, seen->vectors, reg, result);
}

static uint8_t* tier0_test_preset_register(Tier0TestPreset* preset, uint8_t reg, bool result)
{
    return tier0_test_register_bytes(preset->general, &preset->rax, &preset->rdx, preset->vectors, reg, result);
}

// Byte `i` of passed `a`'s bytes (a 10 for a result's) in placement `placement`.
static uint8_t tier0_test_passed_byte(uint32_t a, uint32_t i, uint32_t placement)
{
    const uint32_t mixed = (a * 131u + i * 29u + placement * 71u + 13u) * 2654435761u;
    return (uint8_t)(mixed >> 24);
}

// A value argument's or parameter's bits.
static uint64_t tier0_test_passed_value(const Tier0TestPassed* passed, uint32_t a)
{
    const double floating = 3.25 + (double)a;
    uint64_t bits = 0x0123456789abcdefull ^ ((uint64_t)a << 56);
    if (passed->class == BUSTER_TYPED_CLASS_F64)
    {
        memcpy(&bits, &floating, sizeof(bits));
    }
    return bits;
}

// Whether `recorded` holds `bytes` bytes of `expected` from `offset`, for an error's message too.
static bool tier0_test_same_bytes(const uint8_t* recorded, const uint8_t* expected, uint32_t bytes)
{
    return recorded != 0 && memcmp(recorded, expected, bytes) == 0;
}

// The probe: every register a call passes in recorded into `seen` (Tier0TestSeen: movabs r10, then a store each), the
// stack's slots past its return address too (through r11), then what `preset` holds returned in rax, rdx, xmm0 and
// xmm1, and with its hidden_flag `hidden_bytes` of its hidden bytes written where rdi points (or into its sink: a cmovne
// picks the place) and rdi returned in rax. Returns the count.
static uint32_t tier0_test_aggregate_probe(X86Instruction* records, uint64_t seen, uint64_t preset, uint32_t hidden_bytes)
{
    uint32_t count = 0;
    records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(seen >> 32), (int32_t)(uint32_t)seen);
    count += 1;
    for (uint32_t g = 0; g < BUSTER_ABI_INTEGER_ARGUMENTS; g += 1)
    {
        const int32_t at = (int32_t)(offsetof(Tier0TestSeen, general) + 8 * g);
        records[count] = tier0_instruction(X86_FORM_MOV_RM64_R64, abi_integer_arguments[g], X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, at, 0);
        count += 1;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestSeen, rax), 0);
    count += 1;
    for (uint32_t v = 0; v < BUSTER_ABI_VECTOR_ARGUMENTS; v += 1)
    {
        const int32_t at = (int32_t)(offsetof(Tier0TestSeen, vectors) + 16 * v);
        records[count] = tier0_instruction(X86_FORM_MOVUPS_XM_X, (uint8_t)v, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, at, 0);
        count += 1;
    }
    for (uint32_t w = 0; w < BUSTER_TIER0_TEST_STACK_WORDS; w += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)(8 + 8 * w), 0);
        records[count + 1] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_R11, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE,
                                               (int32_t)(offsetof(Tier0TestSeen, stack) + 8 * w), 0);
        count += 2;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(preset >> 32), (int32_t)(uint32_t)preset);
    records[count + 1] = tier0_instruction(X86_FORM_LEA_R64_M, X86_REGISTER_R11, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestPreset, sink), 0);
    records[count + 2] = tier0_instruction(X86_FORM_CMP_RM64_IMM8, 0, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestPreset, hidden_flag), 0);
    records[count + 3] = tier0_instruction(X86_FORM_CMOVNE_R64_RM64, X86_REGISTER_R11, X86_REGISTER_RDI, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    count += 4;
    for (uint32_t w = 0; w < hidden_bytes / 8; w += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestPreset, hidden) + 8 * w), 0);
        records[count + 1] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R11, BUSTER_X86_MEMORY_BASE, (int32_t)(8 * w), 0);
        count += 2;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestPreset, rax), 0);
    records[count + 1] = tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestPreset, rdx), 0);
    records[count + 2] = tier0_instruction(X86_FORM_MOVUPS_X_XM, 0, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestPreset, vectors), 0);
    records[count + 3] = tier0_instruction(X86_FORM_MOVUPS_X_XM, 1, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestPreset, vectors) + 16), 0);
    records[count + 4] = tier0_instruction(X86_FORM_CMOVNE_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    records[count + 5] = tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0);
    return count + 6;
}

// The driver: `stack_words` of `preset`'s stack slots stored below a frame that leaves rsp a multiple of 16 at the call,
// every argument register loaded from it (the general ones through r10, last), the function at `callee` called through
// r11, and rax, rdx, xmm0 and xmm1 recorded into `seen`. Returns the count.
static uint32_t tier0_test_aggregate_driver(X86Instruction* records, uint64_t preset, uint64_t seen, uint64_t callee, uint32_t stack_words)
{
    const int32_t frame = (int32_t)((8 * stack_words + 15) / 16 * 16 + 8);
    uint32_t count = 0;
    records[count] = tier0_instruction(X86_FORM_SUB_RM64_IMM32, 0, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, frame);
    records[count + 1] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(preset >> 32), (int32_t)(uint32_t)preset);
    count += 2;
    for (uint32_t w = 0; w < stack_words; w += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestPreset, stack) + 8 * w), 0);
        records[count + 1] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, BUSTER_X86_MEMORY_BASE, (int32_t)(8 * w), 0);
        count += 2;
    }
    for (uint32_t v = 0; v < BUSTER_ABI_VECTOR_ARGUMENTS; v += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOVUPS_X_XM, (uint8_t)v, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestPreset, vectors) + 16 * v), 0);
        count += 1;
    }
    for (uint32_t g = 0; g < BUSTER_ABI_INTEGER_ARGUMENTS; g += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_R64_RM64, abi_integer_arguments[g], X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestPreset, general) + 8 * g), 0);
        count += 1;
    }
    records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R11, 0, (int32_t)(callee >> 32), (int32_t)(uint32_t)callee);
    records[count + 1] = tier0_instruction(X86_FORM_CALL_RM64, 0, X86_REGISTER_R11, BUSTER_X86_REGISTER_DIRECT, 0, 0);
    records[count + 2] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(seen >> 32), (int32_t)(uint32_t)seen);
    records[count + 3] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestSeen, rax), 0);
    records[count + 4] = tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RDX, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestSeen, rdx), 0);
    records[count + 5] = tier0_instruction(X86_FORM_MOVUPS_XM_X, 0, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestSeen, vectors), 0);
    records[count + 6] = tier0_instruction(X86_FORM_MOVUPS_XM_X, 1, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(offsetof(Tier0TestSeen, vectors) + 16), 0);
    records[count + 7] = tier0_instruction(X86_FORM_ADD_RM64_IMM32, 0, X86_REGISTER_RSP, BUSTER_X86_REGISTER_DIRECT, 0, frame);
    records[count + 8] = tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0);
    return count + 9;
}

// A fixture's caller: the probe's address (the callee, pushed first), each aggregate's address (`addresses`) and each
// value, the call, its result object (local 0) copied to `out`, and a return. Its columns are its own.
typedef struct Tier0TestCaller Tier0TestCaller;
struct Tier0TestCaller
{
    uint8_t kinds[128];
    uint8_t classes[128];
    uint64_t values[128];
    uint8_t argument_classes[BUSTER_TIER0_TEST_PASSED];
    uint32_t argument_sizes[BUSTER_TIER0_TEST_PASSED];
    AbiLocation locations[BUSTER_TIER0_TEST_PASSED];
    AbiCall call;
    uint8_t local_classes[1];
    uint8_t local_flags[1];
    uint32_t local_sizes[1];
    uint8_t local_alignments[1];
    SymbolId target;
    uint32_t first;
    uint32_t argument_count;
    uint8_t variadic;
    uint32_t result_object;
    Tier0Function function;
};

static void tier0_test_aggregate_caller(Tier0TestCaller* caller, const Tier0TestPassing* fixture, const uint64_t* addresses, uint64_t probe, uint64_t out, bool deep)
{
    memset(caller, 0, sizeof(*caller));
    uint32_t count = 0;
    for (uint32_t stack = 0; deep && stack < 2; stack += 1)
    {
        for (uint32_t value = 0; value < 16; value += 1)
        {
            caller->kinds[count] = BUSTER_TYPED_CONST;
            caller->classes[count] = stack == 0 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_F64;
            caller->values[count] = UINT64_C(0x3ff0000000000000) + value;
            count += 1;
        }
    }
    caller->kinds[count] = BUSTER_TYPED_CONST;
    caller->classes[count] = BUSTER_TYPED_CLASS_U64;
    caller->values[count] = probe;
    count += 1;
    for (uint32_t a = 0; a < fixture->count; a += 1)
    {
        const Tier0TestPassed* passed = &fixture->passed[a];
        caller->kinds[count] = BUSTER_TYPED_CONST;
        caller->classes[count] = passed->class;
        caller->values[count] = passed->size != 0 ? addresses[a] : tier0_test_passed_value(passed, a);
        count += 1;
        caller->argument_classes[a] = passed->class;
        caller->argument_sizes[a] = passed->size;
        caller->locations[a] = passed->location;
    }
    caller->kinds[count] = BUSTER_TYPED_CALL;
    caller->classes[count] = BUSTER_TYPED_CLASS_VOID;
    count += 1;
    if (fixture->result_size != 0)
    {
        caller->kinds[count] = BUSTER_TYPED_CONST;
        caller->classes[count] = BUSTER_TYPED_CLASS_U64;
        caller->values[count] = out;
        caller->kinds[count + 1] = BUSTER_TYPED_LOCAL_ADDRESS;
        caller->classes[count + 1] = BUSTER_TYPED_CLASS_U64;
        caller->kinds[count + 2] = BUSTER_TYPED_COPY;
        caller->classes[count + 2] = BUSTER_TYPED_CLASS_VOID;
        caller->values[count + 2] = fixture->result_size;
        count += 3;
    }
    for (uint32_t stack = 0; deep && stack < 2; stack += 1)
    {
        // Adding zero establishes postorder's last class after CALL/COPY, and when the two stacks change class.
        caller->kinds[count] = BUSTER_TYPED_CONST;
        caller->classes[count] = stack == 0 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_U64;
        caller->kinds[count + 1] = BUSTER_TYPED_ADD;
        caller->classes[count + 1] = caller->classes[count];
        count += 2;
        for (uint32_t value = 0; value < 16; value += 1)
        {
            caller->kinds[count] = BUSTER_TYPED_DISCARD;
            caller->classes[count] = stack == 0 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_U64;
            count += 1;
        }
    }
    caller->kinds[count] = BUSTER_TYPED_RETURN;
    caller->classes[count] = BUSTER_TYPED_CLASS_VOID;
    count += 1;
    caller->call = (AbiCall){
        .stack_bytes = fixture->stack_bytes,
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .result = fixture->result,
    };
    caller->local_classes[0] = BUSTER_TYPED_CLASS_VOID;
    caller->local_flags[0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
    caller->local_sizes[0] = fixture->result_size != 0 ? fixture->result_size : 8;
    caller->local_alignments[0] = 3;
    caller->argument_count = fixture->count;
    caller->result_object = fixture->result_size != 0 ? 1 : 0;
    Tier0Function* function = &caller->function;
    function->kinds = caller->kinds;
    function->classes = caller->classes;
    function->values = caller->values;
    function->count = count;
    function->local_classes = caller->local_classes;
    function->local_flags = caller->local_flags;
    function->local_sizes = caller->local_sizes;
    function->local_alignments = caller->local_alignments;
    function->local_count = 1;
    function->result = (AbiReturn)BUSTER_TIER0_TEST_NO_RESULT;
    function->call_targets = &caller->target;
    function->call_first = &caller->first;
    function->call_argument_counts = &caller->argument_count;
    function->call_variadic = &caller->variadic;
    function->calls = &caller->call;
    function->argument_locations = caller->locations;
    function->argument_classes = caller->argument_classes;
    function->argument_sizes = caller->argument_sizes;
    function->call_result_objects = &caller->result_object;
    function->call_count = 1;
    function->argument_count = fixture->count;
}

// A fixture's function: its parameters (after the hidden pointer, for a result through one), each aggregate's bytes
// and each value copied to its slot of `out`, with each aggregate's address after its bytes (at
// BUSTER_TIER0_TEST_ADDRESS_AT, so that its alignment shows), and its result, whose bytes are at `source`: the address
// returned for one in registers, copied where the hidden pointer points (and it returned) for one through it.
typedef struct Tier0TestCallee Tier0TestCallee;
struct Tier0TestCallee
{
    uint8_t kinds[80];
    uint8_t classes[80];
    uint64_t values[80];
    uint8_t local_classes[1 + BUSTER_TIER0_TEST_PASSED];
    uint8_t local_flags[1 + BUSTER_TIER0_TEST_PASSED];
    uint32_t local_sizes[1 + BUSTER_TIER0_TEST_PASSED];
    uint8_t local_alignments[1 + BUSTER_TIER0_TEST_PASSED];
    AbiLocation parameters[1 + BUSTER_TIER0_TEST_PASSED];
    Tier0Function function;
};

static void tier0_test_aggregate_callee(Tier0TestCallee* callee, const Tier0TestPassing* fixture, uint64_t out, uint64_t source)
{
    memset(callee, 0, sizeof(*callee));
    const bool hidden = fixture->result.kind == BUSTER_ABI_RESULT_MEMORY;
    const uint32_t first = hidden ? 1 : 0;
    if (hidden)
    {
        callee->local_classes[0] = BUSTER_TYPED_CLASS_U64;
        callee->parameters[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    }
    uint32_t count = 0;
    for (uint32_t a = 0; a < fixture->count; a += 1)
    {
        const Tier0TestPassed* passed = &fixture->passed[a];
        const uint32_t local = first + a;
        const bool object = passed->size != 0;
        callee->local_classes[local] = object ? BUSTER_TYPED_CLASS_VOID : passed->class;
        callee->local_flags[local] = object ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
        callee->local_sizes[local] = passed->size;
        callee->local_alignments[local] = object ? passed->alignment_log2 : 0;
        callee->parameters[local] = passed->location;
        callee->kinds[count] = BUSTER_TYPED_CONST;
        callee->classes[count] = BUSTER_TYPED_CLASS_U64;
        callee->values[count] = out + BUSTER_TIER0_TEST_SLOT_BYTES * a;
        callee->kinds[count + 1] = object ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL;
        callee->classes[count + 1] = object ? BUSTER_TYPED_CLASS_U64 : passed->class;
        callee->values[count + 1] = local;
        callee->kinds[count + 2] = object ? BUSTER_TYPED_COPY : BUSTER_TYPED_SET;
        callee->classes[count + 2] = object ? BUSTER_TYPED_CLASS_VOID : passed->class;
        callee->values[count + 2] = passed->size;
        count += 3;
        if (object)
        {
            callee->kinds[count] = BUSTER_TYPED_CONST;
            callee->classes[count] = BUSTER_TYPED_CLASS_U64;
            callee->values[count] = out + BUSTER_TIER0_TEST_SLOT_BYTES * a + BUSTER_TIER0_TEST_ADDRESS_AT;
            callee->kinds[count + 1] = BUSTER_TYPED_LOCAL_ADDRESS;
            callee->classes[count + 1] = BUSTER_TYPED_CLASS_U64;
            callee->values[count + 1] = local;
            callee->kinds[count + 2] = BUSTER_TYPED_SET;
            callee->classes[count + 2] = BUSTER_TYPED_CLASS_U64;
            count += 3;
        }
    }
    if (hidden)
    {
        static const uint8_t kinds[5] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_COPY, BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN };
        static const uint8_t classes[5] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
        const uint64_t values[5] = { 0, source, fixture->result_size, 0, 0 };
        memcpy(callee->kinds + count, kinds, sizeof(kinds));
        memcpy(callee->classes + count, classes, sizeof(classes));
        memcpy(callee->values + count, values, sizeof(values));
        count += 5;
    }
    else if (fixture->result_size != 0)
    {
        callee->kinds[count] = BUSTER_TYPED_CONST;
        callee->classes[count] = BUSTER_TYPED_CLASS_U64;
        callee->values[count] = source;
        callee->kinds[count + 1] = BUSTER_TYPED_RETURN;
        callee->classes[count + 1] = BUSTER_TYPED_CLASS_U64;
        count += 2;
    }
    else
    {
        callee->kinds[count] = BUSTER_TYPED_RETURN;
        callee->classes[count] = BUSTER_TYPED_CLASS_VOID;
        count += 1;
    }
    Tier0Function* function = &callee->function;
    function->kinds = callee->kinds;
    function->classes = callee->classes;
    function->values = callee->values;
    function->count = count;
    function->local_classes = callee->local_classes;
    function->local_flags = callee->local_flags;
    function->local_sizes = callee->local_sizes;
    function->local_alignments = callee->local_alignments;
    function->local_count = first + fixture->count;
    function->parameter_count = first + fixture->count;
    function->parameters = callee->parameters;
    function->result = fixture->result;
    function->result_size = hidden ? 0 : fixture->result_size;
}

// A function's code by both paths, which must agree and be valid, encoded by each path's encoder of its own (the vector
// path's by x86_encode, the scalar's by its twin) into `images` (a page each), which must agree too.
static bool tier0_test_aggregate_encode(Tier0TestTally* tally, const char* check, const char* name, const Tier0Function* function, Arena* arena, uint8_t* images)
{
    const Tier0Check validation = tier0_validate(function);
    const Tier0Code scalar = tier0_compile_scalar(function, arena);
    const Tier0Code vector = tier0_compile(function, arena);
    bool ready = tier0_test_expect(tally, validation.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), check,
                                   "%s: rule \"%s\" at %u, error %d, or the paths differ", name, validation.rule != 0 ? validation.rule : "", validation.node, scalar.error);
    ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.branch_count == 0 && scalar.reference_count == 0,
                                       check, "%s: an invalid record, a branch or a reference", name);
    int64_t lengths[2] = { -1, -1 };
    for (uint32_t encoder = 0; encoder < 2 && ready; encoder += 1)
    {
        const Tier0Code* code = encoder == 0 ? &vector : &scalar;
        const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(code->count);
        uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, room);
        lengths[encoder] = bytes == 0 ? -1 : encoder == 0 ? x86_encode(code->records, code->count, bytes, room, 0, 0) : x86_encode_scalar(code->records, code->count, bytes, room, 0, 0);
        ready = tier0_test_expect(tally, lengths[encoder] > 0 && lengths[encoder] <= BUSTER_TIER0_TEST_PAGE, check, "%s: encoder %u failed", name, encoder);
        if (ready)
        {
            memcpy(images + encoder * BUSTER_TIER0_TEST_PAGE, bytes, (size_t)lengths[encoder]);
        }
    }
    return ready && tier0_test_expect(tally, lengths[0] == lengths[1] && memcmp(images, images + BUSTER_TIER0_TEST_PAGE, (size_t)lengths[0]) == 0, check,
                                      "%s: the encoders differ", name);
}

// Records encoded by the scalar encoder into a page.
static bool tier0_test_aggregate_load(const X86Instruction* records, uint32_t count, uint8_t* page)
{
    return x86_validate(records, count) == count && x86_encode_scalar(records, count, page, BUSTER_TIER0_TEST_PAGE, 0, 0) > 0;
}

// The pages for the passed bytes: for each of a fixture's passed aggregates and its result, a page between two
// PROT_NONE ones, its bytes at its end (placement 0) and at its start (placement 1).
#define BUSTER_TIER0_TEST_DATA_SLOTS (BUSTER_TIER0_TEST_PASSED + 1)

static uint8_t* tier0_test_aggregate_data(void)
{
    uint8_t* data = mmap(0, 3 * BUSTER_TIER0_TEST_DATA_SLOTS * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guarded = data != MAP_FAILED;
    for (uint32_t s = 0; s < BUSTER_TIER0_TEST_DATA_SLOTS && guarded; s += 1)
    {
        guarded = mprotect(data + 3 * s * BUSTER_TIER0_TEST_PAGE, BUSTER_TIER0_TEST_PAGE, PROT_NONE) == 0 &&
                  mprotect(data + (3 * s + 2) * BUSTER_TIER0_TEST_PAGE, BUSTER_TIER0_TEST_PAGE, PROT_NONE) == 0;
    }
    if (data != MAP_FAILED && !guarded)
    {
        munmap(data, 3 * BUSTER_TIER0_TEST_DATA_SLOTS * BUSTER_TIER0_TEST_PAGE);
    }
    return guarded ? data : 0;
}

// Where slot `s`'s `size` bytes are in `placement`, and the bytes there written.
static uint8_t* tier0_test_aggregate_place(uint8_t* data, uint32_t s, uint32_t size, uint32_t placement)
{
    uint8_t* page = data + (3 * s + 1) * BUSTER_TIER0_TEST_PAGE;
    uint8_t* bytes = placement == 0 ? page + BUSTER_TIER0_TEST_PAGE - size : page;
    for (uint32_t i = 0; i < size; i += 1)
    {
        bytes[i] = tier0_test_passed_byte(s, i, placement);
    }
    return bytes;
}

// Runs `count` pages of `code` in a child, each from runs[0] poisoned, which is then copied to its own run's: the
// function at page `first + r`'s address, called with no arguments. Returns whether the child exited with status 0.
static bool tier0_test_aggregate_run(Tier0TestShared* shared, uint8_t* code, uint32_t first)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_AGGREGATE_RUNS; r += 1)
        {
            typedef void (*Tier0TestEntryPoint)(void);
            Tier0TestEntryPoint entry = 0;
            void* address = code + (first + r) * BUSTER_TIER0_TEST_PAGE;
            memcpy(&entry, &address, sizeof(entry));
            memset(&shared->runs[0], 0xa5, sizeof(shared->runs[0]));
            entry();
            shared->runs[1 + r] = shared->runs[0];
        }
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Each fixture as a call: the caller, for each placement of the aggregates' bytes, by both paths, called in a child,
// passes them to the probe, which records what it gets: each value and each part in its register, each part on the
// stack and each aggregate on the stack whole in its slots. A result in registers that the probe returns must land in
// the caller's object, which it copies out, as must one the probe writes through the hidden pointer.
static void tier0_test_aggregate_calls(Tier0TestTally* tally)
{
    static const char check[] = "aggregate calls";
    Tier0TestShared* shared = mmap(0, sizeof(Tier0TestShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* data = tier0_test_aggregate_data();
    // Per fixture: the callers (2 placements by 2 encoders), then the probe.
    const uint64_t pages = (BUSTER_TIER0_TEST_AGGREGATE_RUNS + 1) * BUSTER_TIER0_TEST_PAGE;
    uint8_t* code = mmap(0, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && data != 0 && code != MAP_FAILED && arena.base != 0, check, "no memory");
    for (uint32_t f = 0; f < 2 * BUSTER_ARRAY_LENGTH(tier0_test_passings) && ready; f += 1)
    {
        const Tier0TestPassing* fixture = &tier0_test_passings[f % BUSTER_ARRAY_LENGTH(tier0_test_passings)];
        const bool deep = f >= BUSTER_ARRAY_LENGTH(tier0_test_passings);
        arena_pop_to(&arena, 0);
        ready = mprotect(code, pages, PROT_READ | PROT_WRITE) == 0;
        const uint8_t* bytes[2][BUSTER_TIER0_TEST_PASSED];
        for (uint32_t placement = 0; placement < 2 && ready; placement += 1)
        {
            uint64_t addresses[BUSTER_TIER0_TEST_PASSED];
            for (uint32_t a = 0; a < fixture->count; a += 1)
            {
                bytes[placement][a] = fixture->passed[a].size != 0 ? tier0_test_aggregate_place(data, a, fixture->passed[a].size, placement) : 0;
                addresses[a] = (uint64_t)(uintptr_t)bytes[placement][a];
            }
            Tier0TestCaller caller;
            tier0_test_aggregate_caller(&caller, fixture, addresses, (uint64_t)(uintptr_t)(code + BUSTER_TIER0_TEST_AGGREGATE_RUNS * BUSTER_TIER0_TEST_PAGE),
                                        (uint64_t)(uintptr_t)shared->runs[0].out, deep);
            ready = tier0_test_aggregate_encode(tally, check, fixture->name, &caller.function, &arena, code + 2 * placement * BUSTER_TIER0_TEST_PAGE);
        }
        // What the probe returns: registers of bytes of their own, and for a hidden result its bytes.
        memset(&shared->preset, 0, sizeof(shared->preset));
        shared->preset.rax = 0xa1a2a3a4a5a6a7a8ull;
        shared->preset.rdx = 0xb1b2b3b4b5b6b7b8ull;
        for (uint32_t i = 0; i < 16; i += 1)
        {
            shared->preset.vectors[0][i] = (uint8_t)(0xc0 + i);
            shared->preset.vectors[1][i] = (uint8_t)(0xd0 + i);
        }
        const bool hidden = fixture->result.kind == BUSTER_ABI_RESULT_MEMORY;
        shared->preset.hidden_flag = hidden ? 1 : 0;
        for (uint32_t i = 0; i < BUSTER_TIER0_TEST_HIDDEN_BYTES; i += 1)
        {
            shared->preset.hidden[i] = (uint8_t)(0xe0 ^ (7 * i));
        }
        X86Instruction probe[128];
        const uint32_t probe_count = tier0_test_aggregate_probe(probe, (uint64_t)(uintptr_t)&shared->runs[0], (uint64_t)(uintptr_t)&shared->preset,
                                                                hidden ? fixture->result_size : 0);
        ready = ready && tier0_test_expect(tally, tier0_test_aggregate_load(probe, probe_count, code + BUSTER_TIER0_TEST_AGGREGATE_RUNS * BUSTER_TIER0_TEST_PAGE) &&
                                                      mprotect(code, pages, PROT_READ | PROT_EXEC) == 0,
                                           check, "%s: the probe can't be loaded", fixture->name);
        ready = ready && tier0_test_expect(tally, tier0_test_aggregate_run(shared, code, 0), check, "%s: the child failed", fixture->name);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_AGGREGATE_RUNS && ready; r += 1)
        {
            Tier0TestSeen* seen = &shared->runs[1 + r];
            const uint32_t placement = r / 2;
            for (uint32_t a = 0; a < fixture->count; a += 1)
            {
                const Tier0TestPassed* passed = &fixture->passed[a];
                const AbiLocation location = passed->location;
                bool same = true;
                if (passed->size == 0)
                {
                    uint8_t value[8];
                    const uint64_t bits = tier0_test_passed_value(passed, a);
                    memcpy(value, &bits, sizeof(value));
                    const uint8_t* recorded = location.kind == BUSTER_ABI_LOCATION_STACK ? seen->stack + location.stack_offset
                                                                                       : tier0_test_seen_register(seen, tier0_abi_register(location.registers[0]), false);
                    same = tier0_test_same_bytes(recorded, value, 8);
                }
                else if (tier0_aggregate_whole(location))
                {
                    same = tier0_test_same_bytes(seen->stack + location.stack_offset, bytes[placement][a], passed->size);
                }
                else
                {
                    Tier0Part parts[2];
                    const uint32_t part_count = tier0_location_parts(location, passed->size, parts);
                    for (uint32_t k = 0; k < part_count; k += 1)
                    {
                        const uint8_t* recorded = parts[k].reg == BUSTER_TIER0_PART_STACK ? seen->stack + location.stack_offset : tier0_test_seen_register(seen, parts[k].reg, false);
                        same &= tier0_test_same_bytes(recorded, bytes[placement][a] + parts[k].offset, parts[k].bytes);
                    }
                }
                tier0_test_expect(tally, same, check, "%s, run %u: argument %u isn't where its location says", fixture->name, r, a);
            }
            if (hidden)
            {
                tier0_test_expect(tally, memcmp(seen->out, shared->preset.hidden, fixture->result_size) == 0, check, "%s, run %u: the hidden result isn't the object's", fixture->name,
                                  r);
            }
            else if (fixture->result_size != 0)
            {
                Tier0Part parts[2];
                const uint32_t part_count = tier0_result_parts(fixture->result, fixture->result_size, parts);
                bool same = part_count != 0;
                for (uint32_t k = 0; k < part_count; k += 1)
                {
                    same &= tier0_test_same_bytes(seen->out + parts[k].offset, tier0_test_preset_register(&shared->preset, parts[k].reg, true), parts[k].bytes);
                }
                tier0_test_expect(tally, same, check, "%s, run %u: the result's registers aren't the object's bytes", fixture->name, r);
            }
        }
    }
    arena_release(&arena);
    if (code != MAP_FAILED)
    {
        munmap(code, pages);
    }
    if (data != 0)
    {
        munmap(data, 3 * BUSTER_TIER0_TEST_DATA_SLOTS * BUSTER_TIER0_TEST_PAGE);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestShared));
    }
}

// Each fixture as a function: a driver passes the aggregates' bytes as its locations say (each register's and each
// slot's other bytes their own, which no parameter may read), and the function, by both paths, copies each parameter
// out, which must be its bytes; it returns bytes placed at a PROT_NONE page's edge, both ways, in registers, which the
// driver records, or through the hidden pointer it passes, which must come back in rax.
static void tier0_test_aggregate_functions(Tier0TestTally* tally)
{
    static const char check[] = "aggregate functions";
    Tier0TestShared* shared = mmap(0, sizeof(Tier0TestShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* data = tier0_test_aggregate_data();
    // Per fixture: the functions (2 placements of the result by 2 encoders), then a driver of each.
    const uint64_t pages = 2 * BUSTER_TIER0_TEST_AGGREGATE_RUNS * BUSTER_TIER0_TEST_PAGE;
    uint8_t* code = mmap(0, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && data != 0 && code != MAP_FAILED && arena.base != 0, check, "no memory");
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(tier0_test_passings) && ready; f += 1)
    {
        const Tier0TestPassing* fixture = &tier0_test_passings[f];
        const bool hidden = fixture->result.kind == BUSTER_ABI_RESULT_MEMORY;
        arena_pop_to(&arena, 0);
        ready = mprotect(code, pages, PROT_READ | PROT_WRITE) == 0;
        // The parameters' bytes (placement 0's), and the registers and slots that pass them, every other byte its own.
        const uint8_t* bytes[BUSTER_TIER0_TEST_PASSED];
        memset(&shared->preset, 0, sizeof(shared->preset));
        for (uint32_t i = 0; i < sizeof(shared->preset.general) + 16 + sizeof(shared->preset.vectors) + sizeof(shared->preset.stack); i += 1)
        {
            ((uint8_t*)&shared->preset)[i] = (uint8_t)(0x5c ^ (3 * i));
        }
        for (uint32_t a = 0; a < fixture->count; a += 1)
        {
            const Tier0TestPassed* passed = &fixture->passed[a];
            const AbiLocation location = passed->location;
            bytes[a] = passed->size != 0 ? tier0_test_aggregate_place(data, a, passed->size, 0) : 0;
            if (passed->size == 0)
            {
                const uint64_t bits = tier0_test_passed_value(passed, a);
                uint8_t* place = location.kind == BUSTER_ABI_LOCATION_STACK ? shared->preset.stack + location.stack_offset
                                                                            : tier0_test_preset_register(&shared->preset, tier0_abi_register(location.registers[0]), false);
                memcpy(place, &bits, sizeof(bits));
            }
            else if (tier0_aggregate_whole(location))
            {
                memcpy(shared->preset.stack + location.stack_offset, bytes[a], passed->size);
            }
            else
            {
                Tier0Part parts[2];
                const uint32_t part_count = tier0_location_parts(location, passed->size, parts);
                for (uint32_t k = 0; k < part_count; k += 1)
                {
                    uint8_t* place = parts[k].reg == BUSTER_TIER0_PART_STACK ? shared->preset.stack + location.stack_offset : tier0_test_preset_register(&shared->preset, parts[k].reg, false);
                    memcpy(place, bytes[a] + parts[k].offset, parts[k].bytes);
                }
            }
        }
        if (hidden)
        {
            shared->preset.general[0] = (uint64_t)(uintptr_t)shared->runs[0].hidden;
        }
        const uint8_t* source[2];
        for (uint32_t placement = 0; placement < 2 && ready; placement += 1)
        {
            source[placement] = tier0_test_aggregate_place(data, BUSTER_TIER0_TEST_PASSED, fixture->result_size != 0 ? fixture->result_size : 1, placement);
            Tier0TestCallee callee;
            tier0_test_aggregate_callee(&callee, fixture, (uint64_t)(uintptr_t)shared->runs[0].out, (uint64_t)(uintptr_t)source[placement]);
            ready = tier0_test_aggregate_encode(tally, check, fixture->name, &callee.function, &arena, code + 2 * placement * BUSTER_TIER0_TEST_PAGE);
        }
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_AGGREGATE_RUNS && ready; r += 1)
        {
            X86Instruction driver[128];
            const uint32_t driver_count = tier0_test_aggregate_driver(driver, (uint64_t)(uintptr_t)&shared->preset, (uint64_t)(uintptr_t)&shared->runs[0],
                                                                      (uint64_t)(uintptr_t)(code + r * BUSTER_TIER0_TEST_PAGE), (fixture->stack_bytes + 7) / 8);
            ready = tier0_test_expect(tally, tier0_test_aggregate_load(driver, driver_count, code + (BUSTER_TIER0_TEST_AGGREGATE_RUNS + r) * BUSTER_TIER0_TEST_PAGE), check,
                                      "%s: the driver can't be loaded", fixture->name);
        }
        ready = ready && tier0_test_expect(tally, mprotect(code, pages, PROT_READ | PROT_EXEC) == 0 && tier0_test_aggregate_run(shared, code, BUSTER_TIER0_TEST_AGGREGATE_RUNS),
                                           check, "%s: the child failed", fixture->name);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_AGGREGATE_RUNS && ready; r += 1)
        {
            Tier0TestSeen* seen = &shared->runs[1 + r];
            for (uint32_t a = 0; a < fixture->count; a += 1)
            {
                const Tier0TestPassed* passed = &fixture->passed[a];
                const AbiLocation location = passed->location;
                const uint8_t* out = seen->out + BUSTER_TIER0_TEST_SLOT_BYTES * a;
                bool same = true;
                if (passed->size == 0)
                {
                    const uint64_t bits = tier0_test_passed_value(passed, a);
                    same = memcmp(out, &bits, sizeof(bits)) == 0;
                }
                else if (tier0_aggregate_whole(location))
                {
                    same = memcmp(out, bytes[a], passed->size) == 0;
                }
                else
                {
                    Tier0Part parts[2];
                    const uint32_t part_count = tier0_location_parts(location, passed->size, parts);
                    for (uint32_t k = 0; k < part_count; k += 1)
                    {
                        same &= memcmp(out + parts[k].offset, bytes[a] + parts[k].offset, parts[k].bytes) == 0;
                    }
                }
                uint64_t address = 0;
                memcpy(&address, out + BUSTER_TIER0_TEST_ADDRESS_AT, sizeof(address));
                same &= passed->size == 0 || address % (1u << passed->alignment_log2) == 0;
                tier0_test_expect(tally, same, check, "%s, run %u: parameter %u isn't what its location passed, or isn't aligned", fixture->name, r, a);
            }
            const uint8_t* expected = source[r / 2];
            if (hidden)
            {
                tier0_test_expect(tally, seen->rax == (uint64_t)(uintptr_t)shared->runs[0].hidden && memcmp(seen->hidden, expected, fixture->result_size) == 0, check,
                                  "%s, run %u: the hidden result isn't the function's, or its pointer isn't returned", fixture->name, r);
            }
            else if (fixture->result_size != 0)
            {
                Tier0Part parts[2];
                const uint32_t part_count = tier0_result_parts(fixture->result, fixture->result_size, parts);
                bool same = part_count != 0;
                for (uint32_t k = 0; k < part_count; k += 1)
                {
                    same &= tier0_test_same_bytes(tier0_test_seen_register(seen, parts[k].reg, true), expected + parts[k].offset, parts[k].bytes);
                }
                tier0_test_expect(tally, same, check, "%s, run %u: the result's registers aren't its bytes", fixture->name, r);
            }
        }
    }
    arena_release(&arena);
    if (code != MAP_FAILED)
    {
        munmap(code, pages);
    }
    if (data != 0)
    {
        munmap(data, 3 * BUSTER_TIER0_TEST_DATA_SLOTS * BUSTER_TIER0_TEST_PAGE);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestShared));
    }
}

// A change to a fixture's caller (or with `callee`, its function) that tier0_validate must refuse, and whether it is
// one of the aggregates' metadata, which both compile paths refuse too (tier0_frame_input_valid); else the rule it
// breaks, which `rule` begins.
typedef struct Tier0TestBreak Tier0TestBreak;
struct Tier0TestBreak
{
    const char* name;
    uint8_t fixture;
    bool callee;
    const char* rule;
};

#define BUSTER_TIER0_TEST_METADATA "invalid incoming variadic metadata, address-only local role, call or parameter location, or aggregate metadata"

static const Tier0TestBreak tier0_test_breaks[] = {
    { "an aggregate past the most bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "17 bytes in registers", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a missing first part without HIGH", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a second part of 8 bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "HIGH of 8 bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "HIGH in two registers", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "SPLIT in two registers", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "SPLIT in no register", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "SPLIT of 8 bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "SPLIT with HIGH", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a slot at an offset not a multiple of 8", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a high eightbyte's slot past the call's area", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a SPLIT's slot past the call's area", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a high eightbyte on the stack with a register", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a high eightbyte on the stack of 8 bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a high eightbyte on the stack of 17 bytes", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a register two aggregates take", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a register an aggregate and a value take", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "rsp as a part's register", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "xmm16 as a part's register", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "an aggregate whole past the call's area", 0, false, BUSTER_TIER0_TEST_METADATA },
    { "a call's area aligned past 2^30", 0, false, BUSTER_TIER0_TEST_METADATA },
    { "an aggregate's address of class I64", 2, false, "a call's argument whose class doesn't fit its location" },
    { "a value's class unlike its register", 2, false, "a call's argument whose class doesn't fit its location" },
    { "a result object that is a value", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a result object past the locals", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a result object too small for its registers", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a result in an x87 register", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a result in three registers", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a call with a result object, of class I64", 3, false, "a call whose result goes to its object, with a class" },
    { "a hidden result with rdi an argument's", 12, false, BUSTER_TIER0_TEST_METADATA },
    { "a hidden result without an object", 12, false, BUSTER_TIER0_TEST_METADATA },
    { "a result size with a hidden result", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "a result size of 17 bytes", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "a return of I64 with a result size", 3, true, "a return of an aggregate that isn't an address (U64)" },
    { "a function with a result size that reaches its end", 3, true, "a function returning an aggregate that reaches its end" },
    { "an object parameter aligned past 2^30", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "an object parameter of 17 bytes in registers", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "an object parameter of class I64", 3, true, "an object that isn't an address-taken local of class VOID" },
    { "a hidden result without its pointer in rdi", 12, true, BUSTER_TIER0_TEST_METADATA },
    { "a hidden result whose pointer is an object", 12, true, BUSTER_TIER0_TEST_METADATA },
    { "a variadic function's SPLIT slot past its incoming area", 2, true, BUSTER_TIER0_TEST_METADATA },
    { "a function with a result size that jumps to its end", 3, true, "a function returning an aggregate that reaches its end" },
    { "a function with a result size that branches to its end", 3, true, "a function returning an aggregate that reaches its end" },
    { "a jump to the end with a value left", 2, true, "values left at the function's end" },
    { "an object parameter's two parts in one register", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "an object parameter's part in rax", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "two object parameters in one register", 0, true, BUSTER_TIER0_TEST_METADATA },
    { "a value parameter in an object parameter's register", 12, true, BUSTER_TIER0_TEST_METADATA },
    { "a value parameter in xmm8", 1, true, BUSTER_TIER0_TEST_METADATA },
    { "an aggregate argument's two parts in one register", 0, false, BUSTER_TIER0_TEST_METADATA },
    { "a value argument in rax", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "an aggregate argument's part in xmm8", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a result in rax twice", 3, true, BUSTER_TIER0_TEST_METADATA },
    { "a call's result in rax and rbx", 3, false, BUSTER_TIER0_TEST_METADATA },
    { "a return of VOID with a hidden result", 12, true, "a return of an aggregate that isn't an address (U64)" },
    { "a function with a hidden result that reaches its end", 12, true, "a function returning an aggregate that reaches its end" },
    { "a low eightbyte on the stack of 8 bytes", 13, false, BUSTER_TIER0_TEST_METADATA },
    { "a low eightbyte on the stack with HIGH", 13, false, BUSTER_TIER0_TEST_METADATA },
    { "LOW in registers", 2, false, BUSTER_TIER0_TEST_METADATA },
    { "a low eightbyte's slot past the call's area", 13, false, BUSTER_TIER0_TEST_METADATA },
};

// Applies break `b` to a caller or a function built from its fixture.
static void tier0_test_aggregate_break(uint32_t b, Tier0TestCaller* caller, Tier0TestCallee* callee, AbiCall* incoming)
{
    uint32_t* sizes = caller->argument_sizes;
    AbiLocation* locations = caller->locations;
    switch (b)
    {
        case 0:
            sizes[5] = BUSTER_TIER0_MAX_ARGUMENT_BYTES + 1;
            break;
        case 1:
            sizes[0] = 17;
            break;
        case 2:
            locations[0] = (AbiLocation){ .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_R9 }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
            break;
        case 3:
            locations[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_R9 }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
            sizes[0] = 8;
            break;
        case 4:
            sizes[0] = 8;
            break;
        case 5:
            locations[0].registers[1] = BUSTER_ABI_R9;
            break;
        case 6:
            locations[3].registers[1] = BUSTER_ABI_R9;
            break;
        case 7:
            locations[3].registers[0] = BUSTER_ABI_REGISTER_NONE;
            break;
        case 8:
            sizes[3] = 8;
            break;
        case 9:
            locations[3].flags = BUSTER_ABI_FLAG_HIGH;
            break;
        case 10:
            locations[6].stack_offset = 12;
            break;
        case 11:
            locations[6].stack_offset = 40;
            break;
        case 12:
            locations[9].stack_offset = 40;
            break;
        case 13:
            locations[6].registers[0] = BUSTER_ABI_R9;
            break;
        case 14:
            sizes[6] = 8;
            break;
        case 15:
            sizes[6] = 17;
            break;
        case 16:
            locations[2].registers[0] = BUSTER_ABI_RDI;
            break;
        case 17:
            locations[7].registers[0] = BUSTER_ABI_RSI;
            break;
        case 18:
            locations[0].registers[0] = BUSTER_ABI_RSP;
            break;
        case 19:
            locations[1].registers[0] = BUSTER_TIER0_TEST_X(16);
            break;
        case 20:
            locations[6].stack_offset = 80;
            break;
        case 21:
            caller->call.stack_alignment_log2 = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 + 1;
            break;
        case 22:
            caller->argument_classes[0] = BUSTER_TYPED_CLASS_I64;
            caller->classes[1] = BUSTER_TYPED_CLASS_I64;
            break;
        case 23:
            caller->argument_classes[7] = BUSTER_TYPED_CLASS_F64;
            caller->classes[8] = BUSTER_TYPED_CLASS_F64;
            break;
        case 24:
            caller->local_sizes[0] = 0;
            caller->local_classes[0] = BUSTER_TYPED_CLASS_U64;
            break;
        case 25:
            caller->result_object = 2;
            break;
        case 26:
            caller->local_sizes[0] = 4;
            break;
        case 27:
            caller->call.result.registers[0] = BUSTER_ABI_REGISTER_X87;
            break;
        case 28:
            caller->call.result.registers[2] = BUSTER_ABI_RCX;
            break;
        case 29:
            caller->classes[2] = BUSTER_TYPED_CLASS_I64;
            break;
        case 30:
            locations[1].registers[0] = BUSTER_ABI_RDI;
            break;
        case 31:
            caller->result_object = 0;
            break;
        case 32:
            callee->function.result.kind = BUSTER_ABI_RESULT_MEMORY;
            break;
        case 33:
            callee->function.result_size = 17;
            break;
        case 34:
            callee->classes[callee->function.count - 2] = BUSTER_TYPED_CLASS_I64;
            callee->classes[callee->function.count - 1] = BUSTER_TYPED_CLASS_I64;
            break;
        case 35:
            callee->kinds[callee->function.count - 1] = BUSTER_TYPED_DISCARD;
            break;
        case 36:
            callee->local_alignments[0] = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 + 1;
            break;
        case 37:
            callee->local_sizes[0] = 17;
            break;
        case 38:
            callee->local_classes[0] = BUSTER_TYPED_CLASS_I64;
            break;
        case 39:
            callee->parameters[0].registers[0] = BUSTER_ABI_R9;
            break;
        case 40:
            callee->local_sizes[0] = 8;
            callee->local_classes[0] = BUSTER_TYPED_CLASS_VOID;
            callee->local_flags[0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            callee->local_alignments[0] = 3;
            break;
        case 41:
            *incoming = (AbiCall){ .stack_bytes = 32, .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 6, .vector_registers = 3 };
            callee->function.incoming_variadic = incoming;
            break;
        case 42:
            // A jump to the end in place of the RETURN and its address.
            callee->function.count -= 1;
            callee->kinds[callee->function.count - 1] = BUSTER_TYPED_JUMP;
            callee->classes[callee->function.count - 1] = BUSTER_TYPED_CLASS_VOID;
            callee->values[callee->function.count - 1] = callee->function.count;
            break;
        case 43:
        {
            // A branch to the end before the RETURN and its address, which only the branch passes by.
            const uint32_t at = callee->function.count - 2;
            for (uint32_t i = callee->function.count + 1; i >= at + 2; i -= 1)
            {
                callee->kinds[i] = callee->kinds[i - 2];
                callee->classes[i] = callee->classes[i - 2];
                callee->values[i] = callee->values[i - 2];
            }
            callee->function.count += 2;
            callee->kinds[at] = BUSTER_TYPED_CONST;
            callee->classes[at] = BUSTER_TYPED_CLASS_I32;
            callee->values[at] = 0;
            callee->kinds[at + 1] = BUSTER_TYPED_BRANCH_FALSE;
            callee->classes[at + 1] = BUSTER_TYPED_CLASS_I32;
            callee->values[at + 1] = callee->function.count;
            break;
        }
        case 44:
            // A value in place of the RETURN, taken to the end by JUMP_VALUE.
            callee->kinds[callee->function.count - 1] = BUSTER_TYPED_CONST;
            callee->classes[callee->function.count - 1] = BUSTER_TYPED_CLASS_I64;
            callee->values[callee->function.count - 1] = 1;
            callee->kinds[callee->function.count] = BUSTER_TYPED_JUMP_VALUE;
            callee->classes[callee->function.count] = BUSTER_TYPED_CLASS_I64;
            callee->function.count += 1;
            callee->values[callee->function.count - 1] = callee->function.count;
            break;
        case 45:
            callee->parameters[0].registers[1] = BUSTER_ABI_RDI;
            break;
        case 46:
            callee->parameters[0].registers[0] = BUSTER_ABI_RAX;
            break;
        case 47:
            callee->parameters[2].registers[0] = BUSTER_ABI_RDX;
            break;
        case 48:
            callee->parameters[2].registers[0] = BUSTER_ABI_RDX;
            break;
        case 49:
            callee->parameters[6].registers[0] = BUSTER_TIER0_TEST_X(8);
            break;
        case 50:
            locations[0].registers[1] = BUSTER_ABI_RDI;
            break;
        case 51:
            locations[7].registers[0] = BUSTER_ABI_RAX;
            break;
        case 52:
            locations[1].registers[0] = BUSTER_TIER0_TEST_X(8);
            break;
        case 53:
            callee->function.result.registers[1] = BUSTER_ABI_RAX;
            break;
        case 54:
            caller->call.result.registers[1] = BUSTER_ABI_RBX;
            break;
        case 55:
            // A RETURN of nothing in place of the pointer and its RETURN.
            callee->function.count -= 1;
            callee->kinds[callee->function.count - 1] = BUSTER_TYPED_RETURN;
            callee->classes[callee->function.count - 1] = BUSTER_TYPED_CLASS_VOID;
            callee->values[callee->function.count - 1] = 0;
            break;
        case 56:
            // The pointer and its RETURN left out, so that the copy falls off the end.
            callee->function.count -= 2;
            break;
        case 57:
            sizes[0] = 8;
            break;
        case 58:
            locations[0].flags |= BUSTER_ABI_FLAG_HIGH;
            break;
        case 59:
            locations[0].flags = BUSTER_ABI_FLAG_LOW;
            break;
        case 60:
            locations[2].stack_offset = 32;
            break;
        default:
            break;
    }
}

// Each break of the fixtures' callers and functions, built afresh: tier0_validate must refuse it by its rule, and for
// the metadata both compile paths must refuse it too; the fixtures themselves must be valid.
static void tier0_test_aggregate_errors(Tier0TestTally* tally)
{
    static const char check[] = "aggregate errors";
    static uint8_t bytes[64];
    Arena arena = arena_reserve(1ull << 24);
    uint64_t addresses[BUSTER_TIER0_TEST_PASSED];
    for (uint32_t a = 0; a < BUSTER_TIER0_TEST_PASSED; a += 1)
    {
        addresses[a] = (uint64_t)(uintptr_t)bytes;
    }
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(tier0_test_breaks) && arena.base != 0; b += 1)
    {
        const Tier0TestBreak* broken = &tier0_test_breaks[b];
        const Tier0TestPassing* fixture = &tier0_test_passings[broken->fixture];
        Tier0TestCaller caller;
        Tier0TestCallee callee;
        AbiCall incoming;
        tier0_test_aggregate_caller(&caller, fixture, addresses, 0x1000, 0x2000, false);
        tier0_test_aggregate_callee(&callee, fixture, 0x3000, 0x4000);
        const Tier0Function* function = broken->callee ? &callee.function : &caller.function;
        const Tier0Check before = tier0_validate(function);
        tier0_test_aggregate_break(b, &caller, &callee, &incoming);
        const Tier0Check after = tier0_validate(function);
        const bool metadata = strcmp(broken->rule, BUSTER_TIER0_TEST_METADATA) == 0;
        const Tier0Code scalar = metadata ? tier0_compile_scalar(function, &arena) : (Tier0Code){ .error = TIER0_ERROR_UNSUPPORTED };
        const Tier0Code vector = metadata ? tier0_compile(function, &arena) : (Tier0Code){ .error = TIER0_ERROR_UNSUPPORTED };
        const bool refused = after.error == TIER0_ERROR_INVALID && after.rule != 0 && strncmp(after.rule, broken->rule, strlen(broken->rule)) == 0;
        tier0_test_expect(tally, before.error == TIER0_OK && refused && scalar.error == TIER0_ERROR_UNSUPPORTED && vector.error == TIER0_ERROR_UNSUPPORTED, check,
                          "%s: before it, \"%s\"; after it, \"%s\" at %u, and the paths' errors %d and %d", broken->name, before.rule != 0 ? before.rule : "valid",
                          after.rule != 0 ? after.rule : "valid", after.node, scalar.error, vector.error);
        arena_pop_to(&arena, 0);
    }
    arena_release(&arena);
}

// A change to a function of values alone, with nothing that makes the compile paths check its parameters (tier0_validate,
// their precondition, does): parameter `parameter` moved to register `reg`, which tier0_validate must refuse by `rule`.
typedef struct Tier0TestParameterBreak Tier0TestParameterBreak;
struct Tier0TestParameterBreak
{
    const char* name;
    uint32_t parameter;
    uint8_t reg;
    const char* rule;
};

static const Tier0TestParameterBreak tier0_test_parameter_breaks[] = {
    { "two parameters in rdi", 1, BUSTER_ABI_RDI, BUSTER_TIER0_TEST_METADATA },
    { "a parameter in rbx", 1, BUSTER_ABI_RBX, BUSTER_TIER0_TEST_METADATA },
    { "a parameter in xmm8", 2, BUSTER_TIER0_TEST_X(8), BUSTER_TIER0_TEST_METADATA },
    { "an F64 parameter in rdx", 2, BUSTER_ABI_RDX, "a parameter's location doesn't fit its class" },
};

// A function of an I64 in rdi, an I64 in rsi and an F64 in xmm0, which returns: valid, and each break of it refused.
static void tier0_test_parameter_errors(Tier0TestTally* tally)
{
    static const char check[] = "parameter errors";
    for (uint32_t b = 0; b <= BUSTER_ARRAY_LENGTH(tier0_test_parameter_breaks); b += 1)
    {
        const bool broken = b < BUSTER_ARRAY_LENGTH(tier0_test_parameter_breaks);
        uint8_t kinds[1] = { BUSTER_TYPED_RETURN };
        uint8_t classes[1] = { BUSTER_TYPED_CLASS_VOID };
        uint64_t values[1] = { 0 };
        uint8_t local_classes[3] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_F64 };
        uint8_t local_flags[3] = { 0, 0, 0 };
        AbiLocation parameters[3] = {
            { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
            { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
            { .registers = { BUSTER_TIER0_TEST_X(0), BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        };
        if (broken)
        {
            parameters[tier0_test_parameter_breaks[b].parameter].registers[0] = tier0_test_parameter_breaks[b].reg;
        }
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
        function.count = 1;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = 3;
        function.parameter_count = 3;
        function.parameters = parameters;
        const Tier0Check checked = tier0_validate(&function);
        const char* rule = broken ? tier0_test_parameter_breaks[b].rule : 0;
        const bool expected = broken ? checked.error == TIER0_ERROR_INVALID && checked.rule != 0 && strncmp(checked.rule, rule, strlen(rule)) == 0 : checked.error == TIER0_OK;
        tier0_test_expect(tally, expected, check, "%s: \"%s\"", broken ? tier0_test_parameter_breaks[b].name : "the function itself",
                          checked.rule != 0 ? checked.rule : "valid");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The stack's allocations

// The stack check's program: f0(a0 ... a7) = ((a0 * 31 + a1) * 31 + ...) * 31 + a7, which takes two arguments on the
// stack, and entry(n, m, x), which n times saves the stack's position, allocates m * 8 + 65536 bytes (8 MB of stack
// would last 128 times without the restores), sets their first and last words, calls f0 with them among its arguments
// (stored below the allocation, in the outgoing area), with sum * 31 live across the call (in the save area, from rbp),
// adds the address modulo 16 (0) and both words, which the call's arguments mustn't have overwritten, then restores the
// position.
#define BUSTER_TIER0_TEST_STACK_ITERATIONS 1000
#define BUSTER_TIER0_TEST_STACK_BYTES 65536
static const Tier0TestEntry tier0_test_stack_callee[] = {
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 4 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 6 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 7 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
};
// The entry's locals: n, m and x, then sum, i, the saved position, the bytes, the allocation and its last word.
enum
{
    TIER0_TEST_STACK_N,
    TIER0_TEST_STACK_M,
    TIER0_TEST_STACK_X,
    TIER0_TEST_STACK_SUM,
    TIER0_TEST_STACK_I,
    TIER0_TEST_STACK_SAVED,
    TIER0_TEST_STACK_SIZE,
    TIER0_TEST_STACK_P,
    TIER0_TEST_STACK_Q,
    TIER0_TEST_STACK_LOCALS,
};
#define BUSTER_TIER0_TEST_STACK_TOP 4
#define BUSTER_TIER0_TEST_STACK_END 71
static const Tier0TestEntry tier0_test_stack_entry[] = {
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I },
    // 4: while (i < n)
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_N },
    { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 }, { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, BUSTER_TIER0_TEST_STACK_END },
    // 8: saved = position; size = m * 8 + 65536; p = alloca(size); q = p + size / 8 - 1 words.
    { BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SAVED },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_M }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_TEST_STACK_BYTES }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SIZE }, { BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SIZE },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_DIV, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 },
    { BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_Q },
    // 26: *p = i; *q = i * 7.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I }, { BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_Q }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 7 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0 },
    // 34: sum += p & 15.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 15 }, { BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM },
    // 40: sum = sum * 31 + f0(*p, 1, 2, 3, 4, 5, 6, *q).
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 31 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 3 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 5 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 6 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_Q }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM },
    // 56: sum += *p ^ *q; the position restored; i += 1.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_P },
    { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_Q }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SAVED }, { BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_I }, { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_TEST_STACK_TOP },
    // 71: return sum.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_STACK_SUM }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
};

// What entry(n, m, x) returns, computed here.
static uint64_t tier0_test_stack_expected(uint64_t n)
{
    uint64_t sum = 0;
    for (uint64_t i = 0; i < n; i += 1)
    {
        const uint64_t arguments[8] = { i, 1, 2, 3, 4, 5, 6, i * 7 };
        uint64_t result = arguments[0];
        for (uint32_t a = 1; a < 8; a += 1)
        {
            result = result * 31 + arguments[a];
        }
        sum = sum * 31 + result;
        sum += i ^ (i * 7);
    }
    return sum;
}

// A function of `count` hand-written entries over U64 locals (`locals`, the first `parameters` its parameters).
static Tier0Function tier0_test_stack_function(const Tier0TestEntry* entries, uint32_t count, uint8_t* kinds, uint8_t* classes, uint64_t* values,
                                               const uint8_t* local_classes, const uint8_t* local_flags, uint32_t locals)
{
    for (uint32_t e = 0; e < count; e += 1)
    {
        kinds[e] = entries[e].kind;
        classes[e] = entries[e].class;
        values[e] = entries[e].value;
    }
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = kinds;
    function.classes = classes;
    function.values = values;
    function.count = count;
    function.local_classes = local_classes;
    function.local_flags = local_flags;
    function.local_count = locals;
    return function;
}

// The stack's allocations: the validator's rules for ALLOCA, STACK_SAVE, STACK_RESTORE and FRAME_ADDRESS; a frame pointer's depths
// (rbp's taken from the table); then the program above, compiled by both paths, its frame pointer in its prologue, run
// natively on 8 inputs, and against the interpreter and this file's own count.
static void tier0_test_stack(Tier0TestTally* tally, Tier0Tables* tables)
{
    static const uint8_t local_classes[TIER0_TEST_STACK_LOCALS] = {
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    static const uint8_t local_flags[TIER0_TEST_STACK_LOCALS];
    uint8_t kinds[128];
    uint8_t classes[128];
    uint64_t values[128];
    // The rules: each break refused at its entry, then the valid stream.
    static const Tier0TestEntry breaks[][3] = {
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 16 }, { BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 16 }, { BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 16 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 16 }, { BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } },
        { { BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0 } },
        { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0 } },
        { { BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_STACK_RESTORE, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } },
    };
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(breaks); b += 1)
    {
        const Tier0Function function = tier0_test_stack_function(breaks[b], 3, kinds, classes, values, local_classes, local_flags, 0);
        const Tier0Check check = tier0_validate(&function);
        const bool valid = b + 1 == BUSTER_ARRAY_LENGTH(breaks);
        tier0_test_expect(tally, valid ? check.error == TIER0_OK : check.error == TIER0_ERROR_INVALID && check.node == 1, "stack",
                          "stream %u: tier0_validate says %s at %u (%s)", b, check.error == TIER0_OK ? "valid" : "invalid", check.node, check.rule != 0 ? check.rule : "");
    }
    // The depths: 14 integer values at once stop short of rbp's depth, and 15 reach it, which a function without a frame
    // pointer takes and one with it (the stack's position saved first) takes to its deep slots (tier0_select_deep). The
    // last value is a DUP, which no add takes as its operand's home, so that every value takes its depth.
    Arena arena = arena_reserve(1ull << 28);
    for (uint32_t values_at_once = 14; values_at_once <= 15; values_at_once += 1)
    {
        for (uint32_t pointer = 0; pointer < 2; pointer += 1)
        {
            Tier0TestEntry entries[40];
            uint32_t count = 0;
            if (pointer != 0)
            {
                entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_CLASS_U64, 0 };
                entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0 };
            }
            for (uint32_t k = 0; k + 1 < values_at_once; k += 1)
            {
                entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, k % 3 };
            }
            entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0 };
            for (uint32_t k = 1; k < values_at_once; k += 1)
            {
                entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 };
            }
            entries[count++] = (Tier0TestEntry){ BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 };
            const Tier0Function function = tier0_test_stack_function(entries, count, kinds, classes, values, local_classes, local_flags, 3);
            const uint64_t mark = arena.length;
            const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
            const Tier0Code vector = tier0_compile(&function, &arena);
            tier0_test_expect(tally, tier0_validate(&function).error == TIER0_OK && scalar.error == TIER0_OK && vector.error == TIER0_OK &&
                                         tier0_test_same_code(&scalar, &vector),
                              "stack", "%u values at once %s a frame pointer: errors %d and %d, or the paths differ", values_at_once, pointer != 0 ? "with" : "without",
                              scalar.error, vector.error);
            arena_pop_to(&arena, mark);
        }
    }
    arena_release(&arena);
    // The program, as a program of two functions without globals.
    enum
    {
        callee_count = BUSTER_ARRAY_LENGTH(tier0_test_stack_callee),
        entry_count = BUSTER_ARRAY_LENGTH(tier0_test_stack_entry),
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(tier0_test_stack_entry) == BUSTER_TIER0_TEST_STACK_END + 2, "the entry's end is its last two entries");
    uint8_t program_kinds[callee_count + entry_count];
    uint8_t program_classes[callee_count + entry_count];
    uint64_t program_values[callee_count + entry_count];
    uint8_t program_local_classes[8 + TIER0_TEST_STACK_LOCALS];
    uint8_t program_local_flags[8 + TIER0_TEST_STACK_LOCALS] = { 0 };
    uint32_t call_callees[1] = { 0 };
    uint32_t call_first[1] = { 0 };
    uint32_t call_argument_counts[1] = { 8 };
    uint8_t call_indirect[1] = { 0 };
    uint8_t argument_classes[8];
    for (uint32_t e = 0; e < callee_count + entry_count; e += 1)
    {
        const Tier0TestEntry* entry = e < callee_count ? &tier0_test_stack_callee[e] : &tier0_test_stack_entry[e - callee_count];
        program_kinds[e] = entry->kind;
        program_classes[e] = entry->class;
        program_values[e] = entry->value;
    }
    memset(program_local_classes, BUSTER_TYPED_CLASS_U64, sizeof(program_local_classes));
    memset(argument_classes, BUSTER_TYPED_CLASS_U64, sizeof(argument_classes));
    Tier0Program* program = calloc(1, sizeof(Tier0Program));
    Tier0Functions* functions = calloc(1, sizeof(Tier0Functions));
    Arena program_arena = arena_reserve(1ull << 30);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&program_arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&program_arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    bool ok = tier0_test_expect(tally, program != 0 && functions != 0 && code != 0 && code_bytes != 0, "stack", "no memory");
    if (ok)
    {
        program->kinds = program_kinds;
        program->classes = program_classes;
        program->values = program_values;
        program->entry_count = callee_count + entry_count;
        program->local_classes = program_local_classes;
        program->local_flags = program_local_flags;
        program->local_count = 8 + TIER0_TEST_STACK_LOCALS;
        program->call_callees = call_callees;
        program->call_first = call_first;
        program->call_argument_counts = call_argument_counts;
        program->call_indirect = call_indirect;
        program->call_count = 1;
        program->argument_classes = argument_classes;
        program->argument_count = 8;
        program->sink = UINT32_MAX;
        program->function_count = 2;
        program->functions[0] = (Tier0GeneratedFunction){ .first_entry = 0, .entry_count = callee_count, .first_local = 0, .local_count = 8, .parameter_count = 8,
                                                          .result_class = BUSTER_TYPED_CLASS_U64 };
        program->functions[1] = (Tier0GeneratedFunction){ .first_entry = callee_count, .entry_count = entry_count, .first_local = 8, .local_count = TIER0_TEST_STACK_LOCALS,
                                                          .parameter_count = 3, .first_call = 0, .call_count = 1, .result_class = BUSTER_TYPED_CLASS_U64 };
        code->code = code_bytes;
        ok = tier0_test_expect(tally, tier0_program_functions(program, tables, &program_arena, functions), "stack", "no room for the signatures");
        ok = ok && tier0_test_build(tally, functions, &program_arena, code, 0);
    }
    if (ok)
    {
        // The callee has no frame pointer, and the entry's prologue starts with push rbp; mov rbp, rsp.
        const Tier0Code callee = tier0_compile(&functions->functions[0], &program_arena);
        const Tier0Code entry = tier0_compile(&functions->functions[1], &program_arena);
        const bool callee_plain = callee.error == TIER0_OK && !(callee.records[0].form == X86_FORM_PUSH_R64 && callee.records[0].rm == X86_REGISTER_RBP);
        const bool entry_pointer = entry.error == TIER0_OK && entry.count >= 2 && entry.records[0].form == X86_FORM_PUSH_R64 &&
                                   entry.records[0].rm == X86_REGISTER_RBP && entry.records[1].form == X86_FORM_MOV_RM64_R64 &&
                                   entry.records[1].rm == X86_REGISTER_RBP && entry.records[1].reg == X86_REGISTER_RSP;
        tier0_test_expect(tally, callee_plain && entry_pointer, "stack", "the frame pointer: the callee's %s, the entry's %s", callee_plain ? "none" : "there",
                          entry_pointer ? "there" : "missing");
        uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            inputs[r][0] = BUSTER_TIER0_TEST_STACK_ITERATIONS + r;
            inputs[r][1] = r;
            inputs[r][2] = 0;
        }
        const Tier0TestData data = tier0_test_layout(program);
        Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
        int status = 0;
        const bool ran = tier0_test_run(program, code, &data, inputs, native, &status);
        tier0_test_expect(tally, ran, "stack", "the program didn't exit normally (status %d, signal %d)", status, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        for (uint32_t r = 0; ran && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            bool fault = false;
            uint8_t globals[8];
            const uint64_t interpreted = tier0_test_interpret(program, functions, &data, globals, inputs[r], &fault);
            const uint64_t expected = tier0_test_stack_expected(inputs[r][0]);
            tier0_test_expect(tally, !fault && interpreted == expected && native[r].result == expected, "stack",
                              "input %u: %016" PRIx64 " natively, %016" PRIx64 " interpreted (%s), %016" PRIx64 " expected", r, native[r].result, interpreted,
                              fault ? "a fault" : "no fault", expected);
        }
    }
    arena_release(&program_arena);
    free(functions);
    free(program);
}

// A frame's address (#148's __builtin_frame_address(0)): f0(a) returns the word its frame address points at, the rbp
// its prologue's push saved, which is its caller's frame address; entry(n, m, x) takes its own (fa), allocates m * 8 + 64
// bytes (p), which move rsp but not the frame pointer, and returns (f0(n) == fa) + 2 * (its frame address again == fa)
// + 4 * (p < fa) + 8 * (fa & 15) + 16 * n: 7 + 16 * n.
enum
{
    TIER0_TEST_FRAME_N,
    TIER0_TEST_FRAME_M,
    TIER0_TEST_FRAME_X,
    TIER0_TEST_FRAME_FA,
    TIER0_TEST_FRAME_P,
    TIER0_TEST_FRAME_LOCALS,
};
static const Tier0TestEntry tier0_test_frame_callee[] = {
    { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
};
static const Tier0TestEntry tier0_test_frame_entry[] = {
    { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_FA },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_M }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 64 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_P },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_N }, { BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_FA }, { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 }, { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_FA },
    { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 }, { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },
    { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_P }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_FA },
    { BUSTER_TYPED_LT, BUSTER_TYPED_CLASS_I32, 0 }, { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4 },
    { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_FA }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 15 }, { BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, TIER0_TEST_FRAME_N }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 16 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
};

// The frame's address: a function of it alone, compiled by both paths alike with a frame pointer and its record (mov
// from rbp at its depth), then the program above, both of its functions with frame pointers, run natively on 8 inputs.
static void tier0_test_frame_address(Tier0TestTally* tally, Tier0Tables* tables)
{
    static const uint8_t local_classes[1] = { BUSTER_TYPED_CLASS_U64 };
    static const uint8_t local_flags[1];
    static const Tier0TestEntry alone[] = {
        { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
    };
    uint8_t kinds[2];
    uint8_t classes[2];
    uint64_t values[2];
    Arena arena = arena_reserve(1ull << 26);
    const Tier0Function function = tier0_test_stack_function(alone, 2, kinds, classes, values, local_classes, local_flags, 0);
    const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
    const Tier0Code vector = tier0_compile(&function, &arena);
    bool read = false;
    for (uint32_t r = 0; vector.error == TIER0_OK && r < vector.count; r += 1)
    {
        read |= vector.records[r].form == X86_FORM_MOV_R64_RM64 && vector.records[r].reg == X86_REGISTER_RAX && vector.records[r].rm == X86_REGISTER_RBP;
    }
    const bool pointer = vector.error == TIER0_OK && vector.count >= 2 && vector.records[0].form == X86_FORM_PUSH_R64 && vector.records[0].rm == X86_REGISTER_RBP &&
                         vector.records[1].form == X86_FORM_MOV_RM64_R64 && vector.records[1].rm == X86_REGISTER_RBP && vector.records[1].reg == X86_REGISTER_RSP;
    tier0_test_expect(tally, tier0_validate(&function).error == TIER0_OK && scalar.error == TIER0_OK && vector.error == TIER0_OK &&
                                 tier0_test_same_code(&scalar, &vector) && pointer && read,
                      "frame address", "alone: errors %d and %d, paths %s, frame pointer %s, rbp %s", scalar.error, vector.error,
                      tier0_test_same_code(&scalar, &vector) ? "alike" : "different", pointer ? "there" : "missing", read ? "read" : "unread");
    arena_release(&arena);
    enum
    {
        callee_count = BUSTER_ARRAY_LENGTH(tier0_test_frame_callee),
        entry_count = BUSTER_ARRAY_LENGTH(tier0_test_frame_entry),
    };
    uint8_t program_kinds[callee_count + entry_count];
    uint8_t program_classes[callee_count + entry_count];
    uint64_t program_values[callee_count + entry_count];
    uint8_t program_local_classes[1 + TIER0_TEST_FRAME_LOCALS];
    uint8_t program_local_flags[1 + TIER0_TEST_FRAME_LOCALS] = { 0 };
    uint32_t call_callees[1] = { 0 };
    uint32_t call_first[1] = { 0 };
    uint32_t call_argument_counts[1] = { 1 };
    uint8_t call_indirect[1] = { 0 };
    uint8_t argument_classes[1] = { BUSTER_TYPED_CLASS_U64 };
    for (uint32_t e = 0; e < callee_count + entry_count; e += 1)
    {
        const Tier0TestEntry* entry = e < callee_count ? &tier0_test_frame_callee[e] : &tier0_test_frame_entry[e - callee_count];
        program_kinds[e] = entry->kind;
        program_classes[e] = entry->class;
        program_values[e] = entry->value;
    }
    memset(program_local_classes, BUSTER_TYPED_CLASS_U64, sizeof(program_local_classes));
    Tier0Program* program = calloc(1, sizeof(Tier0Program));
    Tier0Functions* functions = calloc(1, sizeof(Tier0Functions));
    Arena program_arena = arena_reserve(1ull << 30);
    Tier0TestCode* code = BUSTER_ARENA_PUSH_ARRAY(&program_arena, Tier0TestCode, 1);
    uint8_t* code_bytes = BUSTER_ARENA_PUSH_ARRAY(&program_arena, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES);
    bool ok = tier0_test_expect(tally, program != 0 && functions != 0 && code != 0 && code_bytes != 0, "frame address", "no memory");
    if (ok)
    {
        program->kinds = program_kinds;
        program->classes = program_classes;
        program->values = program_values;
        program->entry_count = callee_count + entry_count;
        program->local_classes = program_local_classes;
        program->local_flags = program_local_flags;
        program->local_count = 1 + TIER0_TEST_FRAME_LOCALS;
        program->call_callees = call_callees;
        program->call_first = call_first;
        program->call_argument_counts = call_argument_counts;
        program->call_indirect = call_indirect;
        program->call_count = 1;
        program->argument_classes = argument_classes;
        program->argument_count = 1;
        program->sink = UINT32_MAX;
        program->function_count = 2;
        program->functions[0] = (Tier0GeneratedFunction){ .first_entry = 0, .entry_count = callee_count, .first_local = 0, .local_count = 1, .parameter_count = 1,
                                                          .result_class = BUSTER_TYPED_CLASS_U64 };
        program->functions[1] = (Tier0GeneratedFunction){ .first_entry = callee_count, .entry_count = entry_count, .first_local = 1, .local_count = TIER0_TEST_FRAME_LOCALS,
                                                          .parameter_count = 3, .first_call = 0, .call_count = 1, .result_class = BUSTER_TYPED_CLASS_U64 };
        code->code = code_bytes;
        ok = tier0_test_expect(tally, tier0_program_functions(program, tables, &program_arena, functions), "frame address", "no room for the signatures");
        ok = ok && tier0_test_build(tally, functions, &program_arena, code, 0);
    }
    if (ok)
    {
        bool pointers = true;
        for (uint32_t f = 0; f < 2; f += 1)
        {
            const Tier0Code compiled = tier0_compile(&functions->functions[f], &program_arena);
            pointers &= compiled.error == TIER0_OK && compiled.count >= 2 && compiled.records[0].form == X86_FORM_PUSH_R64 && compiled.records[0].rm == X86_REGISTER_RBP;
        }
        tier0_test_expect(tally, pointers, "frame address", "a function with a frame address has no frame pointer");
        uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            inputs[r][0] = r;
            inputs[r][1] = r + 1;
            inputs[r][2] = 0;
        }
        const Tier0TestData data = tier0_test_layout(program);
        Tier0TestOutcome native[BUSTER_TIER0_TEST_INPUTS];
        int status = 0;
        const bool ran = tier0_test_run(program, code, &data, inputs, native, &status);
        tier0_test_expect(tally, ran, "frame address", "the program didn't exit normally (status %d, signal %d)", status, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        for (uint32_t r = 0; ran && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            const uint64_t expected = 7 + 16 * inputs[r][0];
            tier0_test_expect(tally, native[r].result == expected, "frame address", "input %u: %016" PRIx64 " natively, %016" PRIx64 " expected", r,
                              native[r].result, expected);
        }
    }
    arena_release(&program_arena);
    free(functions);
    free(program);
}

#include "buster/compiler/tier0_x87_test.c"
#include "buster/compiler/tier0_int128_test.c"
#include "buster/compiler/tier0_vector_test.c"
#include "buster/compiler/tier0_label_test.c"
#include "buster/compiler/tier0_deep_test.c"
#include "buster/compiler/tier0_validate_test.c"

static void tier0_test_entry_repetitions(Tier0TestTally* tally)
{
    Arena arena = arena_reserve(UINT64_C(1) << 27);
    static Tier0Program ordinary;
    static Tier0Program once;
    static Tier0Program repeated;
    static Tier0Program invalid;
    Tier0ProgramOptions options = { .functions = 3, .statements = 4, .expression_nodes = 6, .calls = true, .loops = true, .defined = true };
    bool same = arena.base != NULL && tier0_generate_program(&ordinary, &arena, 88, options, "c_");
    options.entry_repetitions = 1;
    same = same && tier0_generate_program(&once, &arena, 88, options, "c_") &&
           ordinary.entry_count == once.entry_count && ordinary.local_count == once.local_count && ordinary.text_length == once.text_length &&
           memcmp(ordinary.kinds, once.kinds, ordinary.entry_count) == 0 && memcmp(ordinary.classes, once.classes, ordinary.entry_count) == 0 &&
           memcmp(ordinary.values, once.values, (uint64_t)ordinary.entry_count * sizeof(uint64_t)) == 0 &&
           memcmp(ordinary.text, once.text, ordinary.text_length) == 0;
    tier0_test_expect(tally, same, "entry repetitions", "default and one repetition must preserve the ordinary typed stream and generated C");
    options.entry_repetitions = BUSTER_TIER0_GENERATE_ENTRY_REPETITIONS;
    bool callees = same && tier0_generate_program(&repeated, &arena, 88, options, "c_");
    const uint32_t before_entry = same ? ordinary.functions[ordinary.function_count - 1].first_entry : 0;
    callees = callees && repeated.function_count == ordinary.function_count &&
              repeated.functions[repeated.function_count - 1].first_entry == before_entry &&
              memcmp(ordinary.kinds, repeated.kinds, before_entry) == 0 && memcmp(ordinary.classes, repeated.classes, before_entry) == 0 &&
              memcmp(ordinary.values, repeated.values, (uint64_t)before_entry * sizeof(uint64_t)) == 0;
    tier0_test_expect(tally, callees, "entry repetitions", "the repeated entry must retain every existing callee's typed body");
    const uint64_t mark = arena.length;
    options.entry_repetitions = BUSTER_TIER0_GENERATE_ENTRY_REPETITIONS + 1;
    tier0_test_expect(tally, !tier0_generate_program(&invalid, &arena, 88, options, "c_") && arena.length == mark,
                      "entry repetitions", "work past the fixed repetition bound must reject before reserving storage");
    arena_release(&arena);
}

static int run_tier0_tests(int argc, char** argv)
{
    // SOURCE_DATE_EPOCH fixed where it isn't set, so that the compilers' runs it compares with can be kept
    // (oracle_epoch_begin, oracle_cache.c).
    const bool epoch = oracle_epoch_begin();
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
            tier0_test_entry_repetitions(&total);
            static const Tier0ProgramOptions shapes[4] = {
                { .functions = 1, .statements = 6, .expression_nodes = 6, .calls = false, .loops = false, .floating = false, .pointers = false },
                { .functions = 3, .statements = 12, .expression_nodes = 10, .calls = true, .loops = true, .floating = false, .pointers = false, .indirect = true,
                  .access = true },
                { .functions = 4, .statements = 16, .expression_nodes = 14, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true,
                  .access = true, .vlas = true, .unsigned_floating = true },
                { .functions = 8, .statements = 24, .expression_nodes = 20, .calls = true, .loops = true, .floating = true, .pointers = true, .objects = true, .indirect = true,
                  .access = true, .vlas = true, .unsigned_floating = true },
            };
            static const char* const names[4] = { "programs: one function", "programs: integers and calls", "programs: everything", "programs: larger" };
            // The larger shape with records passed and returned by value (#137): a program of its own seed for seed. And
            // past the depth tables (tier0_select_deep): calls of up to 127 arguments, records and deeper expressions. And with
            // objects aligned past 16 bytes too, in realigned frames, with and without variable-length arrays.
            Tier0ProgramOptions records = shapes[3];
            records.records = &tables.records;
            Tier0ProgramOptions deep = shapes[3];
            deep.deep = true;
            deep.records = &tables.records;
            Tier0ProgramOptions aligned = deep;
            aligned.over_aligned = true;
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
                tier0_test_operands(&tally);
                tier0_test_report("operands", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_stack(&tally, &tables);
                tier0_test_report("stack", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_frame_address(&tally, &tables);
                tier0_test_report("frame address", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_labels(&tally);
                tier0_test_report("labels", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_validation(&tally);
                tier0_test_report("validation", tally);
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
                tier0_test_home_candidates(&tally, seed);
                tier0_test_report("home candidates", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_frame_alignment(&tally, seed);
                tier0_test_report("frame alignment", tally);
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
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_unsigned_floating(&tally);
                tier0_test_report("unsigned floating", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_unsigned_limits(&tally);
                tier0_test_report("unsigned floating limits", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_deep(&tally);
                tier0_test_report("deep", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_access(&tally);
                tier0_test_report("access", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_aggregate_errors(&tally);
                tier0_test_report("aggregate errors", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_parameter_errors(&tally);
                tier0_test_report("parameter errors", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_aggregate_calls(&tally);
                tier0_test_wide_call_depths(&tally);
                tier0_test_large_call_records(&tally);
                tier0_test_record_count_bounds(&tally);
                tier0_test_report("aggregate calls", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                tier0_test_aggregate_functions(&tally);
                tier0_test_report("aggregate functions", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            for (uint32_t s = 0; s < 7; s += 1)
            {
                Tier0TestTally tally = { 0, 0 };
                uint64_t instructions = 0;
                uint64_t entries = 0;
                uint64_t deferred = 0;
                uint64_t deep_entries = 0;
                uint64_t frames[2] = { 0, 0 };
                uint64_t coverage[BUSTER_TIER0_TEST_COVERAGE] = { 0 };
                tier0_test_programs(&tally, &tables, seed + s * 1000003ull, count, s < 4 ? shapes[s] : s == 4 ? records : s == 5 ? deep : aligned, &instructions, &entries,
                                    &deferred, &deep_entries, frames, coverage);
                if (s == 4 || s == 5)
                {
                    // With the default count, every kind of part, location and result the catalog can make shows: the
                    // shape is meant to (but an ignored argument with bytes, and an XMM part through a temporary, which
                    // need unnamed bit-fields and halves: the hand-made locations' checks have them).
                    printf("   ");
                    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_COVERAGE; k += 1)
                    {
                        printf(" %s %" PRIu64 "%s", tier0_test_coverage_names[k], coverage[k], k + 1 < BUSTER_TIER0_TEST_COVERAGE ? "," : "\n");
                        tier0_test_expect(&tally, count < 300 || k == 1 || k == 6 || coverage[k] != 0, "records by value", "no %s", tier0_test_coverage_names[k]);
                    }
                }
                // The deep shape's programs must reach the deep slots: with the default count, many of their entries do.
                tier0_test_expect(&tally, s < 5 ? deep_entries == 0 : count < 300 || deep_entries >= entries / 100, "deep values",
                                  "%" PRIu64 " deep entries among %" PRIu64, deep_entries, entries);
                // Frames are realigned for objects aligned past 16 alone, and for the records' aligned to 32: with the default
                // count, the last shape has many, a base among them (with variable-length arrays).
                tier0_test_expect(&tally, s < 4 ? frames[0] == 0 : s < 6 || count < 300 || (frames[0] >= 100 && frames[1] >= 10), "realigned frames",
                                  "%" PRIu64 " realigned frames, %" PRIu64 " with a base", frames[0], frames[1]);
                tier0_test_report(s < 4 ? names[s] : s == 4 ? "programs: records by value" : s == 5 ? "programs: deep values" : "programs: aligned past 16 bytes", tally);
                printf("    %" PRIu64 " entries, %" PRIu64 " records, %" PRIu64 " deferred, %" PRIu64 " deep, %" PRIu64 " realigned frames (%" PRIu64 " with a base)\n",
                       entries, instructions, deferred, deep_entries, frames[0], frames[1]);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                // After the programs: it adds a struct of a long double to the tables, and classifies them again.
                Tier0TestTally tally = { 0, 0 };
                uint32_t too_deep = 0;
                uint64_t entries = 0;
                tier0_test_x87(&tally, &tables, seed, count, &too_deep, &entries);
                tier0_test_report("x87", tally);
                printf("    %" PRIu64 " entries, %u programs past the depths\n", entries, too_deep);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                uint32_t too_deep = 0;
                uint64_t entries = 0;
                tier0_test_int128(&tally, &tables, seed, count, &too_deep, &entries);
                tier0_test_report("int128", tally);
                printf("    %" PRIu64 " entries, %u programs past the depths\n", entries, too_deep);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            {
                Tier0TestTally tally = { 0, 0 };
                uint32_t too_deep = 0;
                uint64_t entries = 0;
                tier0_test_vectors(&tally, &tables, seed, count, &too_deep, &entries);
                tier0_test_report("vectors", tally);
                printf("    %" PRIu64 " entries, %u programs past the depths\n", entries, too_deep);
                total.checks += tally.checks;
                total.failures += tally.failures;
            }
            if (cross)
            {
                Tier0TestTally repetitions = { 0, 0 };
                Tier0ProgramOptions repeated = { .functions = 3, .statements = 4, .expression_nodes = 6, .calls = true, .loops = true, .defined = true };
                static const uint32_t counts[] = { 2, BUSTER_TIER0_GENERATE_ENTRY_REPETITIONS };
                for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(counts); at += 1)
                {
                    repeated.entry_repetitions = counts[at];
                    tier0_test_cross(&repetitions, &tables, seed, 1, repeated, clang, keep);
                }
                tier0_test_report("cross-check: entry repetitions", repetitions);
                total.checks += repetitions.checks;
                total.failures += repetitions.failures;
                Tier0TestTally tally = { 0, 0 };
                Tier0ProgramOptions defined = shapes[3];
                defined.defined = true;
                tier0_test_cross(&tally, &tables, seed, cross_count, defined, clang, keep);
                tier0_test_report("cross-check: clang -O0", tally);
                total.checks += tally.checks;
                total.failures += tally.failures;
                Tier0TestTally mixed = { 0, 0 };
                defined.records = &tables.records;
                tier0_test_cross(&mixed, &tables, seed + 1000003ull, cross_count, defined, clang, keep);
                tier0_test_report("cross-check: records, mixed with clang", mixed);
                total.checks += mixed.checks;
                total.failures += mixed.failures;
                Tier0TestTally deep_cross = { 0, 0 };
                Tier0ProgramOptions deep_defined = deep;
                deep_defined.defined = true;
                tier0_test_cross(&deep_cross, &tables, seed + 2000006ull, cross_count, deep_defined, clang, keep);
                tier0_test_report("cross-check: deep values", deep_cross);
                total.checks += deep_cross.checks;
                total.failures += deep_cross.failures;
                Tier0TestTally aligned_cross = { 0, 0 };
                Tier0ProgramOptions aligned_defined = aligned;
                aligned_defined.defined = true;
                tier0_test_cross(&aligned_cross, &tables, seed + 3000009ull, cross_count, aligned_defined, clang, keep);
                tier0_test_report("cross-check: aligned past 16 bytes", aligned_cross);
                total.checks += aligned_cross.checks;
                total.failures += aligned_cross.failures;
            }
            tier0_tables_close(&tables);
        }
        else
        {
            total.failures += 1;
        }
        oracle_cache_report(oracle_cache(), "test-tier0");
        printf("test-tier0: %" PRIu64 " checks, %" PRIu64 " failed\n", total.checks, total.failures);
        result = total.failures == 0 ? 0 : 1;
    }
    oracle_epoch_end(epoch);
    return result;
}
