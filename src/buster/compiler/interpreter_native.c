// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Native calls for the scalar interpreter (#176), included after interpreter.c and calling_convention.c. Explicit
// symbol bindings own native addresses; this file neither resolves symbols nor synthesizes function pointer casts.
// interpreter_native_invoke accepts the IR's scalar values, and aggregates where a call's ABI records (#131) say how they
// pass: each argument in its location's registers or stack slot, a result in registers or through a hidden pointer, what
// the bridge can't place (a vector register's upper half, st1) refused. I8/I16 arguments without records require the
// explicit extensions column, since IR integer types alone cannot distinguish C's sign/zero extension to 32 bits.
// Vectors, callbacks into interpreted functions, and unpromoted variadic operands are rejected. The bridge loads the SysV
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

// A scalar's place without records, as LLVM's x86-64 assignment gives one: the next general register (two for an i128,
// which LLVM never splits) or vector register of its kind, or an 8-byte stack slot (16 bytes, aligned to 16, for an i128
// and for a long double, which no register takes).
static AbiLocation interpreter_native_place(uint16_t type, uint32_t* integers, uint32_t* vectors, uint64_t* stack_bytes, uint8_t* stack_alignment)
{
    const bool floating = type == IR_F32 || type == IR_F64;
    const bool pair = type == IR_I128;
    const bool wide = pair || type == IR_F80;
    AbiLocation location = {
        .registers = {
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
        },
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
    };
    if (floating && *vectors < BUSTER_ABI_VECTOR_ARGUMENTS)
    {
        location.registers[0] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR + *vectors);
        *vectors += 1;
    }
    else if (!floating && type != IR_F80 && *integers + (pair ? 2u : 1u) <= BUSTER_ABI_INTEGER_ARGUMENTS)
    {
        location.registers[0] = abi_integer_arguments[*integers];
        location.registers[1] = pair ? abi_integer_arguments[*integers + 1] : BUSTER_ABI_REGISTER_NONE;
        *integers += pair ? 2u : 1u;
    }
    else
    {
        location.kind = BUSTER_ABI_LOCATION_STACK;
        location.stack_offset = abi_stack_place(stack_bytes, stack_alignment, wide ? 16 : BUSTER_ABI_SLOT_BYTES, wide ? 4 : BUSTER_ABI_SLOT_LOG2);
    }
    return location;
}

// An argument register's slot where a register save area keeps it (rdi to r9 at 0 to 40, xmm0 to xmm7 in 16-byte slots
// from 48), or UINT32_MAX for a register that passes no argument.
static uint32_t interpreter_native_register_slot(uint8_t r)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
    {
        result = abi_integer_arguments[k] == r ? 8 * k : result;
    }
    if (r >= BUSTER_ABI_REGISTER_VECTOR && r < BUSTER_ABI_REGISTER_VECTOR + BUSTER_ABI_VECTOR_ARGUMENTS)
    {
        result = BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * (r - BUSTER_ABI_REGISTER_VECTOR);
    }
    return result;
}

// Argument registers laid out as a register save area lays them out, and an argument area's bytes from rsp at the call,
// each with an optional map of the bytes defined.
typedef struct InterpreterNativeArea InterpreterNativeArea;
struct InterpreterNativeArea
{
    uint8_t* registers;
    uint8_t* registers_defined;
    uint8_t* stack;
    uint8_t* stack_defined;
    uint64_t stack_capacity;
};

static void interpreter_native_store(uint8_t* bytes, uint8_t* bytes_defined, const uint8_t* source, const uint8_t* source_defined, uint64_t size)
{
    memcpy(bytes, source, size);
    if (bytes_defined != 0)
    {
        if (source_defined != 0)
        {
            memcpy(bytes_defined, source_defined, size);
        }
        else
        {
            memset(bytes_defined, 1, size);
        }
    }
}

// An argument's `size` bytes (and their definedness: all defined without a map) placed as `location` says: its parts,
// an eightbyte each (the high one alone with HIGH), in their registers, a SPLIT's part without one in its 8-byte slot, or
// the whole in its stack slot. False for what can't be placed: a register that passes no argument, a part past its
// location's registers (a vector register's upper half), a slot past the area.
static bool interpreter_native_put(const InterpreterNativeArea* area, AbiLocation location, const uint8_t* bytes, const uint8_t* defined, uint64_t size)
{
    bool result = location.kind == BUSTER_ABI_LOCATION_IGNORE;
    if (location.kind == BUSTER_ABI_LOCATION_STACK)
    {
        // The whole value at its offset; or a part alone in its 8-byte slot (an eightbyte at a time, #765), the value's
        // high eightbyte (HIGH) or its low one (LOW), the other one not passed.
        const bool high = (location.flags & BUSTER_ABI_FLAG_HIGH) != 0;
        const bool low = (location.flags & BUSTER_ABI_FLAG_LOW) != 0;
        const uint64_t first = high ? 8 : 0;
        const uint64_t take = !high && !low ? size : first < size ? (size - first < 8 ? size - first : 8) : 0;
        result = !(high && low) && take != 0 && location.stack_offset <= area->stack_capacity && take <= area->stack_capacity - location.stack_offset;
        if (result)
        {
            interpreter_native_store(area->stack + location.stack_offset, area->stack_defined == 0 ? 0 : area->stack_defined + location.stack_offset, bytes + first,
                                     defined == 0 ? 0 : defined + first, take);
        }
    }
    else if (location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT)
    {
        const uint64_t first = (location.flags & BUSTER_ABI_FLAG_HIGH) != 0 ? 8 : 0;
        const bool split = location.kind == BUSTER_ABI_LOCATION_SPLIT;
        result = size <= first + 16 && (size <= first + 8 || location.registers[1] != BUSTER_ABI_REGISTER_NONE || split);
        for (uint32_t part = 0; result && part < 2; part += 1)
        {
            const uint64_t offset = first + 8 * part;
            const uint64_t take = offset < size ? (size - offset < 8 ? size - offset : 8) : 0;
            const uint8_t r = location.registers[part];
            const uint32_t slot = r == BUSTER_ABI_REGISTER_NONE ? UINT32_MAX : interpreter_native_register_slot(r);
            if (r != BUSTER_ABI_REGISTER_NONE)
            {
                result = slot != UINT32_MAX;
                if (result)
                {
                    interpreter_native_store(area->registers + slot, area->registers_defined == 0 ? 0 : area->registers_defined + slot, bytes + offset, defined == 0 ? 0 : defined + offset, take);
                }
            }
            else if (split && take != 0)
            {
                result = location.stack_offset <= area->stack_capacity && BUSTER_ABI_SLOT_BYTES <= area->stack_capacity - location.stack_offset;
                if (result)
                {
                    interpreter_native_store(area->stack + location.stack_offset, area->stack_defined == 0 ? 0 : area->stack_defined + location.stack_offset, bytes + offset, defined == 0 ? 0 : defined + offset, take);
                }
            }
        }
    }
    return result;
}

// Whether the bridge captures every part of an aggregate result in registers: rax, rdx and xmm0's and xmm1's low
// eightbytes (8-byte parts), and st0's ten bytes (a 16-byte part), not x87's st1; `x87` says whether st0 is one.
static bool interpreter_native_returns(AbiReturn returned, bool* x87)
{
    bool result = returned.kind == BUSTER_ABI_RESULT_REGISTERS;
    *x87 = false;
    for (uint32_t part = 0; result && part < BUSTER_ABI_RESULT_PARTS; part += 1)
    {
        const uint8_t r = returned.registers[part];
        const bool word = r == BUSTER_ABI_RAX || r == BUSTER_ABI_RDX || r == BUSTER_ABI_REGISTER_VECTOR || r == BUSTER_ABI_REGISTER_VECTOR + 1;
        const bool top = r == BUSTER_ABI_REGISTER_X87;
        result = r == BUSTER_ABI_REGISTER_NONE || (word && returned.part_bytes == 8) || (top && returned.part_bytes == 16);
        *x87 = *x87 || top;
    }
    return result;
}

// An aggregate result's parts, as interpreter_native_returns admits them, written to `destination` (part i at byte
// i * part_bytes, past the low eightbyte with HIGH), at most `size` bytes, each byte written then defined.
static void interpreter_native_returned(const InterpreterNativeFrame* frame, AbiReturn returned, uint8_t* destination, uint8_t* defined, uint32_t size)
{
    const uint64_t first = (returned.flags & BUSTER_ABI_FLAG_HIGH) != 0 ? 8 : 0;
    for (uint32_t part = 0; part < BUSTER_ABI_RESULT_PARTS; part += 1)
    {
        const uint8_t r = returned.registers[part];
        const uint64_t offset = first + (uint64_t)part * returned.part_bytes;
        const uint8_t* source = r == BUSTER_ABI_RAX ? (const uint8_t*)&frame->integer_result.low : r == BUSTER_ABI_RDX ? (const uint8_t*)&frame->integer_result.high
                              : r == BUSTER_ABI_REGISTER_VECTOR ? (const uint8_t*)&frame->vector_result.low : r == BUSTER_ABI_REGISTER_VECTOR + 1 ? (const uint8_t*)&frame->vector_result.high
                                                                                                                                              : (const uint8_t*)&frame->x87_result;
        const uint64_t available = r == BUSTER_ABI_REGISTER_X87 ? 10 : 8;
        const uint64_t take = r == BUSTER_ABI_REGISTER_NONE || offset >= size ? 0 : size - offset < available ? size - offset : available;
        if (take != 0)
        {
            memcpy(destination + offset, source, take);
            if (defined != 0)
            {
                memset(defined + offset, 1, take);
            }
        }
    }
}

static bool interpreter_native_type(uint16_t type, bool result)
{
    const bool valid = type <= IR_PTR && (result || type != IR_VOID);
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

typedef struct InterpreterNativeRange InterpreterNativeRange;
struct InterpreterNativeRange
{
    InterpreterMemory* memory;
    uint64_t offset;
};

// Object-representation transfers check bounds and permissions without consuming values or changing definedness.
// interpreter_memory_range is the shared policy; this adapts it to the native bridge's range record.
static bool interpreter_native_range(InterpreterRuntime* runtime, uint64_t address, uint64_t bytes, bool write, InterpreterNativeRange* range)
{
    return interpreter_memory_range(runtime, address, bytes, write, &range->memory, &range->offset);
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
            InterpreterNativeRange range;
            if (flags == BUSTER_INTERPRETER_NATIVE_COPY)
            {
                result = interpreter_native_range(runtime, request->arguments[1].low, size, false, &range);
            }
            if (result)
            {
                result = interpreter_native_range(runtime, request->arguments[0].low, size, true, &range);
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
        if (flags == BUSTER_INTERPRETER_NATIVE_COPY || flags == BUSTER_INTERPRETER_NATIVE_SET)
        {
            const uint64_t size = request->arguments[2].low;
            InterpreterNativeRange destination;
            result = interpreter_native_range(runtime, request->arguments[0].low, size, true, &destination);
            if (result && size != 0 && destination.memory != 0 && destination.memory->defined != 0)
            {
                uint8_t* destination_defined = destination.memory->defined + destination.offset;
                InterpreterNativeRange source = {
                    0
                };
                if (flags == BUSTER_INTERPRETER_NATIVE_COPY)
                {
                    result = interpreter_native_range(runtime, request->arguments[1].low, size, false, &source);
                }
                if (result && source.memory != 0 && source.memory->defined != 0)
                {
                    // Move the metadata with memmove even for memcpy bindings: this also preserves source bits when
                    // a valid memmove's ranges overlap. Diagnostics happen on later typed reads, not byte transfers.
                    memmove(destination_defined, source.memory->defined + source.offset, (size_t)size);
                }
                else if (result)
                {
                    memset(destination_defined, 1, (size_t)size);
                }
            }
        }
        else if (flags == BUSTER_INTERPRETER_NATIVE_FREE)
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
        // Records place every argument and the result, in an area the bridge aligns to 16 alone; a binding with memory
        // flags takes none, and an aggregate result leaves the call no value of its own.
        ok = ok && (request->abi == 0 || (request->passed != 0 && request->binding->flags == 0 && request->abi->stack_bytes <= BUSTER_INTERPRETER_NATIVE_STACK_BYTES &&
                                          request->abi->stack_alignment_log2 <= BUSTER_ABI_STACK_ALIGNMENT_LOG2 && (request->result_size == 0 || request->result_type == IR_VOID)));
    }
    InterpreterNativeFrame frame = {
        0
    };
    uint8_t registers[BUSTER_ABI_SAVE_AREA_BYTES] = {
        0
    };
    uint8_t stack[BUSTER_INTERPRETER_NATIVE_STACK_BYTES] = {
        0
    };
    const InterpreterNativeArea area = {
        .registers = registers,
        .stack = stack,
        .stack_capacity = sizeof(stack),
    };
    uint64_t stack_bytes = 0;
    uint8_t stack_alignment = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    uint32_t integers = 0;
    uint32_t vectors = 0;
    uint8_t* defined = 0;
    const AbiCall* abi = ok ? request->abi : 0;
    const bool aggregate_result = abi != 0 && request->result_size != 0;
    InterpreterNativeRange destination = {
        0
    };
    if (ok)
    {
        frame.address = request->binding->address;
        frame.stack = stack;
        frame.x87 = request->result_type == IR_F80;
        if (aggregate_result)
        {
            // The destination's bytes, which the callee writes through a hidden pointer, the first integer argument, or
            // the bridge from the registers that return them.
            bool x87 = false;
            ok = (request->runtime == 0 || interpreter_native_range(request->runtime, request->destination, request->result_size, true, &destination)) &&
                 (abi->result.kind == BUSTER_ABI_RESULT_MEMORY || interpreter_native_returns(abi->result, &x87));
            frame.x87 = x87;
            if (abi->result.kind == BUSTER_ABI_RESULT_MEMORY)
            {
                memcpy(registers, &request->destination, sizeof(request->destination));
            }
        }
        for (uint32_t i = 0; i < request->argument_count && ok; i += 1)
        {
            const uint16_t type = request->types[i];
            const bool floating = type == IR_F32 || type == IR_F64;
            const bool wide = type == IR_I128 || type == IR_F80;
            const uint32_t size = abi != 0 ? request->passed[i].size : 0;
            const AbiLocation location = abi != 0 ? request->passed[i].location : interpreter_native_place(type, &integers, &vectors, &stack_bytes, &stack_alignment);
            const bool sign = abi != 0 ? (location.flags & BUSTER_ABI_FLAG_SIGN) != 0 : request->extensions != 0 && request->extensions[i] != 0;
            ok = interpreter_native_type(type, false);
            ok = ok && (abi != 0 || request->extensions != 0 || (type != IR_I8 && type != IR_I16));
            ok = ok && (!request->variadic || i < request->named_count || (type != IR_F32 && (type < IR_I1 || type > IR_I16)));
            if (ok && size != 0)
            {
                // An aggregate by value: its bytes at its value's address, read as an object's representation is.
                const uint64_t address = request->arguments[i].low;
                InterpreterNativeRange range;
                ok = type == IR_PTR && (request->runtime == 0 ? address != 0 : interpreter_native_range(request->runtime, address, size, false, &range));
                ok = ok && interpreter_native_put(&area, location, (const uint8_t*)(uintptr_t)address, 0, size);
            }
            else if (ok)
            {
                InterpreterValue value = request->arguments[i];
                if (!floating && !wide)
                {
                    value.low = interpreter_native_narrow(value.low, type, sign);
                }
                ok = interpreter_native_put(&area, location, (const uint8_t*)&value, 0, wide ? 16 : 8);
            }
        }
        ok = ok && interpreter_native_flags(request);
        ok = ok && interpreter_native_memory_prepare(request);
        ok = ok && interpreter_native_defined_prepare(request, &defined);
    }
    if (ok)
    {
        for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
        {
            memcpy(&frame.integers[k], registers + 8 * k, sizeof(frame.integers[k]));
        }
        for (uint32_t k = 0; k < BUSTER_ABI_VECTOR_ARGUMENTS; k += 1)
        {
            memcpy(&frame.vectors[k], registers + BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * k, sizeof(frame.vectors[k]));
        }
        frame.stack_bytes = ((abi != 0 ? abi->stack_bytes : stack_bytes) + 15) & ~15ull;
        frame.vector_count = abi != 0 ? abi->vector_registers : vectors;
        interpreter_native_bridge(&frame);
        InterpreterValue value = {
            0
        };
        const uint16_t type = request->result_type;
        if (aggregate_result)
        {
            uint8_t* bytes = (uint8_t*)(uintptr_t)request->destination;
            uint8_t* bytes_defined = destination.memory != 0 && destination.memory->defined != 0 ? destination.memory->defined + destination.offset : 0;
            if (abi->result.kind == BUSTER_ABI_RESULT_MEMORY)
            {
                if (bytes_defined != 0)
                {
                    memset(bytes_defined, 1, request->result_size);
                }
            }
            else
            {
                interpreter_native_returned(&frame, abi->result, bytes, bytes_defined, request->result_size);
            }
        }
        else if (type == IR_F80)
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
