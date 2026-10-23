// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Native calls for the scalar interpreter (#176), included after interpreter.c and calling_convention.c. Explicit
// symbol bindings own native addresses; this file neither resolves symbols nor synthesizes function pointer casts.
// interpreter_native_invoke accepts the IR's scalar values only. I8/I16 arguments require the explicit extensions
// column, since IR integer types alone cannot distinguish C's sign/zero extension to 32 bits. Aggregates, vectors,
// callbacks into interpreted functions, and unpromoted variadic operands are rejected. The bridge loads the SysV
// argument registers and copies the bounded overflow area; its result captures rax/rdx, xmm0 and optionally st0.
// Native code executes with the process's ordinary authority. The memory flags describe trusted libc-compatible
// malloc/free/realloc/calloc/memcpy/memmove/memset bindings so the interpreter can maintain allocation metadata.
// With undefined-byte checking, heap allocations also own a libc-allocated byte map. The reserved heap owner makes
// that map's lifetime distinct from arena-owned local maps; malloc clears it, calloc sets it, realloc retains its
// prefix, and native free releases it. This oracle intentionally spends one metadata byte per heap byte.

#include <stddef.h>
#include <stdlib.h>

#define BUSTER_INTERPRETER_NATIVE_ARGUMENTS 256u
#define BUSTER_INTERPRETER_NATIVE_STACK_BYTES (BUSTER_INTERPRETER_NATIVE_ARGUMENTS * 16u)

typedef struct InterpreterNativeFrame InterpreterNativeFrame;
struct InterpreterNativeFrame
{
    void* address;
    uint64_t integers[BUSTER_ABI_INTEGER_ARGUMENTS];
    uint64_t vectors[BUSTER_ABI_VECTOR_ARGUMENTS];
    const uint8_t* stack;
    uint64_t stack_bytes;
    uint64_t vector_count;
    InterpreterValue integer_result;
    InterpreterValue vector_result;
    InterpreterValue x87_result;
    uint64_t x87;
};

_Static_assert(BUSTER_ABI_INTEGER_ARGUMENTS == 6 && BUSTER_ABI_VECTOR_ARGUMENTS == 8, "the native bridge implements SysV x86-64");
_Static_assert(offsetof(InterpreterNativeFrame, integers) == 8, "native gp offset");
_Static_assert(offsetof(InterpreterNativeFrame, vectors) == 56, "native xmm offset");
_Static_assert(offsetof(InterpreterNativeFrame, stack) == 120, "native stack source offset");
_Static_assert(offsetof(InterpreterNativeFrame, stack_bytes) == 128, "native stack size offset");
_Static_assert(offsetof(InterpreterNativeFrame, vector_count) == 136, "native al offset");
_Static_assert(offsetof(InterpreterNativeFrame, integer_result) == 144, "native integer result offset");
_Static_assert(offsetof(InterpreterNativeFrame, vector_result) == 160, "native xmm result offset");
_Static_assert(offsetof(InterpreterNativeFrame, x87_result) == 176, "native st0 result offset");
_Static_assert(offsetof(InterpreterNativeFrame, x87) == 192, "native st0 flag offset");

// r12 owns the frame across the foreign call, and rbp owns the original stack. rsp is aligned before the call and
// the outgoing area starts exactly at rsp; the call's return address therefore precedes its first stack argument.
// Only a long-double result is popped from x87, avoiding an exception or an extra value on any other return path.
static __attribute__((naked, noinline)) void interpreter_native_bridge(InterpreterNativeFrame* frame __attribute__((unused)))
{
    __asm__(
        "pushq %rbp\n"
        "movq %rsp, %rbp\n"
        "pushq %r12\n"
        "movq %rdi, %r12\n"
        "andq $-16, %rsp\n"
        "subq 128(%r12), %rsp\n"
        "movq %rsp, %rdi\n"
        "movq 120(%r12), %rsi\n"
        "movq 128(%r12), %rcx\n"
        "rep movsb\n"
        "movq 0(%r12), %r11\n"
        "movq 56(%r12), %xmm0\n"
        "movq 64(%r12), %xmm1\n"
        "movq 72(%r12), %xmm2\n"
        "movq 80(%r12), %xmm3\n"
        "movq 88(%r12), %xmm4\n"
        "movq 96(%r12), %xmm5\n"
        "movq 104(%r12), %xmm6\n"
        "movq 112(%r12), %xmm7\n"
        "movq 8(%r12), %rdi\n"
        "movq 16(%r12), %rsi\n"
        "movq 24(%r12), %rdx\n"
        "movq 32(%r12), %rcx\n"
        "movq 40(%r12), %r8\n"
        "movq 48(%r12), %r9\n"
        "movq 136(%r12), %rax\n"
        "callq *%r11\n"
        "movq %rax, 144(%r12)\n"
        "movq %rdx, 152(%r12)\n"
        "movq %xmm0, 160(%r12)\n"
        "movq %xmm1, 168(%r12)\n"
        "cmpq $0, 192(%r12)\n"
        "je 1f\n"
        "fstpt 176(%r12)\n"
        "1:\n"
        "leaq -8(%rbp), %rsp\n"
        "popq %r12\n"
        "popq %rbp\n"
        "retq\n");
}

static bool interpreter_native_type(uint16_t type, bool result)
{
    const bool valid = type < IR_TYPE_COUNT && (result || type != IR_VOID);
    return valid;
}

static uint64_t interpreter_native_narrow(uint64_t value, uint16_t type, bool sign)
{
    const uint32_t bits = type == IR_I1 ? 1 : type == IR_I8 ? 8 : type == IR_I16 ? 16 : type == IR_I32 ? 32 : 64;
    uint64_t result = value;
    if (bits < 64)
    {
        const uint64_t mask = (1ull << bits) - 1;
        result &= mask;
        if (sign && bits < 32 && type != IR_I1)
        {
            const uint64_t top = 1ull << (bits - 1);
            result = (uint32_t)((result ^ top) - top);
        }
    }
    return result;
}

static bool interpreter_native_flags(const InterpreterNativeRequest* request)
{
    const uint32_t flags = request->binding->flags;
    const uint32_t known = BUSTER_INTERPRETER_NATIVE_ALLOCATE | BUSTER_INTERPRETER_NATIVE_FREE | BUSTER_INTERPRETER_NATIVE_REALLOCATE | BUSTER_INTERPRETER_NATIVE_CALLOC | BUSTER_INTERPRETER_NATIVE_COPY | BUSTER_INTERPRETER_NATIVE_SET;
    bool result = (flags & ~known) == 0 && (flags == 0 || (flags & (flags - 1)) == 0);
    if (result && flags != 0)
    {
        const uint32_t count = request->argument_count;
        const uint16_t* types = request->types;
        const bool pointer_result = request->result_type == IR_PTR;
        if (flags == BUSTER_INTERPRETER_NATIVE_ALLOCATE)
        {
            result = count == 1 && types[0] == IR_I64 && pointer_result;
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_FREE)
        {
            result = count == 1 && types[0] == IR_PTR && request->result_type == IR_VOID;
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE)
        {
            result = count == 2 && types[0] == IR_PTR && types[1] == IR_I64 && pointer_result;
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_CALLOC)
        {
            result = count == 2 && types[0] == IR_I64 && types[1] == IR_I64 && pointer_result;
            if (result && request->arguments[0].low != 0)
            {
                result = request->arguments[1].low <= UINT64_MAX / request->arguments[0].low;
            }
        }
        else
        {
            const uint16_t second_type = flags == BUSTER_INTERPRETER_NATIVE_COPY ? IR_PTR : IR_I32;
            result = count == 3 && types[0] == IR_PTR && types[1] == second_type && types[2] == IR_I64 && pointer_result;
        }
    }
    return result;
}

static bool interpreter_native_memory_prepare(const InterpreterNativeRequest* request)
{
    InterpreterRuntime* runtime = request->runtime;
    const uint32_t flags = request->binding->flags;
    bool result = true;
    if (runtime != 0 && flags != 0)
    {
        if (flags == BUSTER_INTERPRETER_NATIVE_COPY || flags == BUSTER_INTERPRETER_NATIVE_SET)
        {
            const uint64_t size = request->arguments[2].low;
            if (flags == BUSTER_INTERPRETER_NATIVE_COPY)
            {
                result = interpreter_memory_access(runtime, request->arguments[1].low, size, false) == INTERPRETER_OK;
            }
            if (result)
            {
                result = interpreter_memory_access(runtime, request->arguments[0].low, size, true) == INTERPRETER_OK;
            }
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_FREE || flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE)
        {
            bool replaces = false;
            const uint64_t address = request->arguments[0].low;
            if (address != 0)
            {
                for (uint32_t i = 0; i < runtime->memory_count; i += 1)
                {
                    replaces |= (uint64_t)(uintptr_t)runtime->memories[i].bytes == address && runtime->memories[i].owner == BUSTER_INTERPRETER_HEAP_OWNER;
                }
            }
            result = !runtime->check_memory || address == 0 || replaces;
            if (flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE)
            {
                result = result && (replaces || runtime->memory_count < runtime->memory_capacity);
            }
        }
        else
        {
            result = runtime->memory_count < runtime->memory_capacity;
        }
    }
    return result;
}

static bool interpreter_native_defined_prepare(const InterpreterNativeRequest* request, uint8_t** defined)
{
    const uint32_t flags = request->binding->flags;
    bool result = true;
    if (request->runtime != 0 && request->runtime->check_undefined && (flags == BUSTER_INTERPRETER_NATIVE_ALLOCATE || flags == BUSTER_INTERPRETER_NATIVE_CALLOC || flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE))
    {
        uint64_t size = flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE ? request->arguments[1].low : request->arguments[0].low;
        if (flags == BUSTER_INTERPRETER_NATIVE_CALLOC)
        {
            size *= request->arguments[1].low;
        }
        if (size != 0)
        {
            *defined = calloc((size_t)size, 1);
            result = *defined != 0;
            if (result && flags == BUSTER_INTERPRETER_NATIVE_CALLOC)
            {
                memset(*defined, 1, (size_t)size);
            }
        }
    }
    return result;
}

static bool interpreter_native_memory_finish(const InterpreterNativeRequest* request, const InterpreterValue* value, uint8_t** defined)
{
    InterpreterRuntime* runtime = request->runtime;
    const uint32_t flags = request->binding->flags;
    bool result = true;
    if (runtime != 0)
    {
        if (flags == BUSTER_INTERPRETER_NATIVE_FREE)
        {
            for (uint32_t i = 0; i < runtime->memory_count; i += 1)
            {
                if ((uint64_t)(uintptr_t)runtime->memories[i].bytes == request->arguments[0].low && runtime->memories[i].owner == BUSTER_INTERPRETER_HEAP_OWNER)
                {
                    free(runtime->memories[i].defined);
                }
            }
            interpreter_memory_remove(runtime, (uint8_t*)(uintptr_t)request->arguments[0].low);
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_ALLOCATE || flags == BUSTER_INTERPRETER_NATIVE_CALLOC || flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE)
        {
            uint64_t size = request->arguments[0].low;
            if (flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE)
            {
                size = request->arguments[1].low;
                // glibc realloc(p, 0) frees p even when it returns null. A failed nonzero realloc preserves p.
                if (value->low != 0 || size == 0)
                {
                    for (uint32_t i = 0; i < runtime->memory_count; i += 1)
                    {
                        const InterpreterMemory* old = &runtime->memories[i];
                        if ((uint64_t)(uintptr_t)old->bytes == request->arguments[0].low && old->owner == BUSTER_INTERPRETER_HEAP_OWNER)
                        {
                            const uint64_t prefix = old->size < size ? old->size : size;
                            if (*defined != 0 && prefix != 0)
                            {
                                if (old->defined != 0)
                                {
                                    memcpy(*defined, old->defined, (size_t)prefix);
                                }
                                else
                                {
                                    memset(*defined, 1, (size_t)prefix);
                                }
                            }
                            free(old->defined);
                        }
                    }
                    interpreter_memory_remove(runtime, (uint8_t*)(uintptr_t)request->arguments[0].low);
                }
            }
            else if (flags == BUSTER_INTERPRETER_NATIVE_CALLOC)
            {
                size *= request->arguments[1].low;
            }
            if (value->low != 0)
            {
                result = interpreter_memory_add(runtime, (uint8_t*)(uintptr_t)value->low, size, *defined, true, BUSTER_INTERPRETER_HEAP_OWNER);
                if (result)
                {
                    *defined = 0;
                }
            }
        }
    }
    return result;
}

// Execution owns the registry's lifetime, while the interpreted program owns its unfreed heap allocations. Drop
// only the interpreter's auxiliary maps before the arena-backed registry is released, leaving returned pointers valid.
static __attribute__((unused)) void interpreter_native_release(InterpreterRuntime* runtime)
{
    if (runtime != 0)
    {
        for (uint32_t i = 0; i < runtime->memory_count; i += 1)
        {
            if (runtime->memories[i].owner == BUSTER_INTERPRETER_HEAP_OWNER)
            {
                free(runtime->memories[i].defined);
                runtime->memories[i].defined = 0;
            }
        }
    }
}

static bool interpreter_native_invoke(const InterpreterNativeRequest* request, InterpreterValue* result)
{
    bool ok = request != 0 && result != 0;
    if (ok)
    {
        ok = request->binding != 0 && request->binding->address != 0 && request->argument_count <= BUSTER_INTERPRETER_NATIVE_ARGUMENTS;
        ok = ok && (request->argument_count == 0 || (request->types != 0 && request->arguments != 0));
        ok = ok && request->named_count <= request->argument_count && (request->variadic || request->named_count == request->argument_count);
        ok = ok && interpreter_native_type(request->result_type, true);
    }
    InterpreterNativeFrame frame = {
        0
    };
    uint8_t stack[BUSTER_INTERPRETER_NATIVE_STACK_BYTES] = {
        0
    };
    uint64_t stack_bytes = 0;
    uint8_t stack_alignment = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    uint32_t integers = 0;
    uint32_t vectors = 0;
    uint8_t* defined = 0;
    if (ok)
    {
        frame.address = request->binding->address;
        frame.stack = stack;
        frame.x87 = request->result_type == IR_F80;
        for (uint32_t i = 0; i < request->argument_count && ok; i += 1)
        {
            const uint16_t type = request->types[i];
            const bool floating = type == IR_F32 || type == IR_F64;
            const bool pair = type == IR_I128;
            const bool long_double = type == IR_F80;
            const bool sign = request->extensions != 0 && request->extensions[i] != 0;
            ok = interpreter_native_type(type, false);
            ok = ok && (request->extensions != 0 || (type != IR_I8 && type != IR_I16));
            ok = ok && (!request->variadic || i < request->named_count || (type != IR_F32 && (type < IR_I1 || type > IR_I16)));
            if (ok)
            {
                InterpreterValue value = request->arguments[i];
                if (!floating && !pair && !long_double)
                {
                    value.low = interpreter_native_narrow(value.low, type, sign);
                }
                if (floating && vectors < BUSTER_ABI_VECTOR_ARGUMENTS)
                {
                    frame.vectors[vectors++] = value.low;
                }
                else if (!floating && !long_double && integers + (pair ? 2u : 1u) <= BUSTER_ABI_INTEGER_ARGUMENTS)
                {
                    frame.integers[integers++] = value.low;
                    if (pair)
                    {
                        frame.integers[integers++] = value.high;
                    }
                }
                else
                {
                    const uint32_t bytes = pair || long_double ? 16 : BUSTER_ABI_SLOT_BYTES;
                    const uint32_t alignment = pair || long_double ? 4 : BUSTER_ABI_SLOT_LOG2;
                    const uint32_t offset = abi_stack_place(&stack_bytes, &stack_alignment, bytes, alignment);
                    memcpy(stack + offset, &value, bytes);
                }
            }
        }
        ok = ok && interpreter_native_flags(request);
        ok = ok && interpreter_native_memory_prepare(request);
        ok = ok && interpreter_native_defined_prepare(request, &defined);
    }
    if (ok)
    {
        frame.stack_bytes = (stack_bytes + 15) & ~15ull;
        frame.vector_count = vectors;
        interpreter_native_bridge(&frame);
        InterpreterValue value = {
            0
        };
        const uint16_t type = request->result_type;
        if (type == IR_F80)
        {
            value = frame.x87_result;
            value.high &= UINT16_MAX;
        }
        else if (type == IR_F32 || type == IR_F64)
        {
            value.low = type == IR_F32 ? (uint32_t)frame.vector_result.low : frame.vector_result.low;
        }
        else if (type != IR_VOID)
        {
            value = frame.integer_result;
            value.low = interpreter_native_narrow(value.low, type, false);
            value.high = type == IR_I128 ? value.high : 0;
        }
        ok = interpreter_native_memory_finish(request, &value, &defined);
        if (ok)
        {
            *result = value;
        }
    }
    free(defined);
    return ok;
}
