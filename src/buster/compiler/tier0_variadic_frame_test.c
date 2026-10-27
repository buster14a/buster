// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original incoming SysV variadic-frame tests. Include after x86_target_forms.c, tier0_generate.c, branch_sizing.c and
// c_builtin_varargs_lower.c. Clang-compiled callers enter actual generated variadic callees; actual va_start,
// va_copy and scalar va_arg recipes consume their incoming register and stack arguments. A separate vector-input
// caller establishes both halves of every XMM save slot. An intervening symbolic CALL clobbers every caller-saved
// register, and the generated callee snapshots all 176 save bytes before and after it. No host va_list is supplied
// in place of the generated frame. The ordinary fixture uses two real 24-byte list locals without incoming metadata.
// Every fixture also has an object (Tier0Function.local_sizes), laid out after the slots, aligned to 16 or past it (to
// 32, 64, 128 or 4096, in a realigned frame: tier0_vf_object_alignments), whose bytes, alignment and place apart from the
// lists, the canaries, the save area and the overflow area are checked.
// Scalar/vector compilation, both branch-sizing paths and both executable encoder images are checked independently.
// Native execution stays in an alarm-bounded child. These tests establish no source-reader or Tier 1 integration.

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_TIER0_VF_ENTRIES 4096u
#define BUSTER_TIER0_VF_LOCALS 256u
#define BUSTER_TIER0_VF_PARAMETERS 24u
#define BUSTER_TIER0_VF_VALUES 20u
#define BUSTER_TIER0_VF_FIXTURES 6u
#define BUSTER_TIER0_VF_PAGE 4096u
#define BUSTER_TIER0_VF_SAVE_WORDS (BUSTER_ABI_SAVE_AREA_BYTES / sizeof(uint64_t))
#define BUSTER_TIER0_VF_RETURN UINT64_C(0xa148f00d72538496)
#define BUSTER_TIER0_VF_CANARY UINT64_C(0xd17ac3b5896e024f)
#define BUSTER_TIER0_VF_OBJECT_BYTES 40u
#define BUSTER_TIER0_VF_OBJECT_WORDS (BUSTER_TIER0_VF_OBJECT_BYTES / sizeof(uint64_t))
// Each fixture's object's alignment's log2: past 16 bytes, its frame is realigned, its incoming overflow area and named
// stack arguments addressed from rbp.
static const uint8_t tier0_vf_object_alignments[BUSTER_TIER0_VF_FIXTURES] = { 4, 6, 12, 5, 4, 7 };

typedef struct Tier0VfTally Tier0VfTally;
struct Tier0VfTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t native_calls;
};

typedef struct Tier0VfOutput Tier0VfOutput;
struct Tier0VfOutput
{
    uint64_t save_before[BUSTER_TIER0_VF_SAVE_WORDS];
    uint64_t save_after[BUSTER_TIER0_VF_SAVE_WORDS];
    uint64_t named[BUSTER_TIER0_VF_PARAMETERS];
    uint64_t values[BUSTER_TIER0_VF_VALUES];
    uint64_t copy_values[BUSTER_TIER0_VF_VALUES];
    uint64_t initial[3];
    uint64_t copied[3];
    uint64_t final[3];
    uint64_t copy_final[3];
    uint64_t addresses[8];
    uint64_t canaries[3];
    uint64_t object[BUSTER_TIER0_VF_OBJECT_WORDS];
};

typedef struct Tier0VfStream Tier0VfStream;
struct Tier0VfStream
{
    uint8_t kinds[BUSTER_TIER0_VF_ENTRIES];
    uint8_t classes[BUSTER_TIER0_VF_ENTRIES];
    uint64_t values[BUSTER_TIER0_VF_ENTRIES];
    uint8_t local_classes[BUSTER_TIER0_VF_LOCALS];
    uint8_t local_flags[BUSTER_TIER0_VF_LOCALS];
    uint32_t local_sizes[BUSTER_TIER0_VF_LOCALS];
    uint8_t local_alignments[BUSTER_TIER0_VF_LOCALS];
    AbiLocation parameters[BUSTER_TIER0_VF_PARAMETERS];
    AbiCall abi[2];
    SymbolId target;
    uint32_t call_first;
    uint32_t call_arguments;
    uint8_t call_variadic;
    CBuiltinLowerBuffer buffer;
    Tier0Function function;
    uint32_t list[2];
    uint32_t canary[3];
    uint32_t save_anchor;
    uint32_t overflow_anchor;
    uint32_t list_pointer[2];
    uint32_t save_pointer;
    uint32_t overflow_pointer;
    uint32_t object;
    uint32_t kind;
};

typedef struct Tier0VfImage Tier0VfImage;
struct Tier0VfImage
{
    uint8_t* bytes;
    uint64_t mapping_bytes;
    uint32_t length;
    uint32_t trampoline;
    uint32_t trampoline_length;
};

typedef struct Tier0VfNative Tier0VfNative;
struct Tier0VfNative
{
    Tier0VfImage images[2][2];
};

static volatile uint64_t tier0_vf_clobber_calls;

static bool tier0_vf_expect(Tier0VfTally* tally, bool condition, const char* message, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-tier0-variadic-frame: %s (item=%u)\n", message, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static __attribute__((noinline)) void tier0_vf_clobber(void)
{
    tier0_vf_clobber_calls += 1;
    __asm__ volatile(
        "mov $0x51, %%eax\n\t"
        "mov $0x52, %%ecx\n\t"
        "mov $0x53, %%edx\n\t"
        "mov $0x54, %%esi\n\t"
        "mov $0x55, %%edi\n\t"
        "mov $0x56, %%r8d\n\t"
        "mov $0x57, %%r9d\n\t"
        "mov $0x58, %%r10d\n\t"
        "mov $0x59, %%r11d\n\t"
        "pcmpeqd %%xmm0, %%xmm0\n\t"
        "pcmpeqd %%xmm1, %%xmm1\n\t"
        "pcmpeqd %%xmm2, %%xmm2\n\t"
        "pcmpeqd %%xmm3, %%xmm3\n\t"
        "pcmpeqd %%xmm4, %%xmm4\n\t"
        "pcmpeqd %%xmm5, %%xmm5\n\t"
        "pcmpeqd %%xmm6, %%xmm6\n\t"
        "pcmpeqd %%xmm7, %%xmm7\n\t"
        "pcmpeqd %%xmm8, %%xmm8\n\t"
        "pcmpeqd %%xmm9, %%xmm9\n\t"
        "pcmpeqd %%xmm10, %%xmm10\n\t"
        "pcmpeqd %%xmm11, %%xmm11\n\t"
        "pcmpeqd %%xmm12, %%xmm12\n\t"
        "pcmpeqd %%xmm13, %%xmm13\n\t"
        "pcmpeqd %%xmm14, %%xmm14\n\t"
        "pcmpeqd %%xmm15, %%xmm15"
        :
        :
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory");
}

static void tier0_vf_emit(Tier0VfStream* stream, uint32_t kind, uint32_t class, uint64_t value)
{
    const uint32_t entry = stream->buffer.count;
    assert(entry < BUSTER_TIER0_VF_ENTRIES);
    stream->kinds[entry] = (uint8_t)kind;
    stream->classes[entry] = (uint8_t)class;
    stream->values[entry] = value;
    stream->buffer.count += 1;
}

static uint32_t tier0_vf_local(Tier0VfStream* stream, uint32_t class, uint32_t flags)
{
    const uint32_t local = stream->buffer.local_count;
    assert(local < BUSTER_TIER0_VF_LOCALS);
    stream->local_classes[local] = (uint8_t)class;
    stream->local_flags[local] = (uint8_t)flags;
    stream->buffer.local_count += 1;
    return local;
}

static void tier0_vf_address(Tier0VfStream* stream, uint32_t local, uint32_t offset)
{
    tier0_vf_emit(stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
    if (offset != 0)
    {
        tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset);
        tier0_vf_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void tier0_vf_output(Tier0VfStream* stream, uint32_t offset)
{
    tier0_vf_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    if (offset != 0)
    {
        tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset);
        tier0_vf_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void tier0_vf_object_output(Tier0VfStream* stream, uint32_t local, uint32_t offset, uint32_t words)
{
    for (uint32_t word = 0; word < words; word += 1)
    {
        tier0_vf_output(stream, offset + word * sizeof(uint64_t));
        tier0_vf_address(stream, local, word * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void tier0_vf_save_output(Tier0VfStream* stream, uint32_t offset)
{
    for (uint32_t word = 0; word < BUSTER_TIER0_VF_SAVE_WORDS; word += 1)
    {
        tier0_vf_output(stream, offset + word * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, stream->save_pointer);
        tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, word * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_vf_emit(stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static bool tier0_vf_recipe(Tier0VfTally* tally, Tier0VfStream* stream, const CBuiltinVaLowerCall* call, bool scalar)
{
    const CBuiltinLowerResult lowered = scalar ? cbuiltin_varargs_lower_scalar(&stream->buffer, call) : cbuiltin_varargs_lower(&stream->buffer, call);
    const bool result = tier0_vf_expect(tally, lowered.error == CBUILTIN_LOWER_OK, "actual bounded varargs recipe accepts its frame operands", call->id);
    return result;
}

static uint32_t tier0_vf_count(uint32_t kind)
{
    const uint32_t result = kind == 0 ? 10 : kind == 1 ? 18 : kind == 2 || kind == 5 ? 8 : 0;
    return result;
}

static uint32_t tier0_vf_class(uint32_t kind, uint32_t value)
{
    const uint32_t result = kind != 0 && ((value & 1) != 0 || (kind == 1 && value >= 16)) ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_U64;
    return result;
}

static bool tier0_vf_argument(Tier0VfTally* tally, Tier0VfStream* stream, uint32_t list, uint32_t value, bool copy, bool scalar)
{
    const uint32_t class = tier0_vf_class(stream->kind, value);
    tier0_vf_output(stream, (copy ? offsetof(Tier0VfOutput, copy_values) : offsetof(Tier0VfOutput, values)) + value * sizeof(uint64_t));
    const CBuiltinVaLowerCall call =
    {
        .id = CBUILTIN_VA_ARG,
        .list = list,
        .result_class = class,
        .plan =
        {
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
            .integer_registers = class == BUSTER_TYPED_CLASS_U64,
            .vector_registers = class == BUSTER_TYPED_CLASS_F64,
            .sources =
            {
                class == BUSTER_TYPED_CLASS_U64 ? BUSTER_ABI_VA_GENERAL : BUSTER_ABI_VA_VECTOR,
                BUSTER_ABI_VA_NONE,
            },
            .overflow_alignment_log2 = 3,
            .stack_bytes = 8,
        },
    };
    const bool result = tier0_vf_recipe(tally, stream, &call, scalar);
    tier0_vf_emit(stream, BUSTER_TYPED_SET, class, 0);
    return result;
}

static bool tier0_vf_fixture(Tier0VfTally* tally, Tier0VfStream* stream, const Tier0Tables* tables, uint32_t kind, bool scalar)
{
    memset(stream, 0, sizeof(*stream));
    stream->kind = kind;
    stream->buffer = (CBuiltinLowerBuffer)
    {
        .kinds = stream->kinds,
        .classes = stream->classes,
        .values = stream->values,
        .capacity = BUSTER_TIER0_VF_ENTRIES,
        .local_classes = stream->local_classes,
        .local_flags = stream->local_flags,
        .local_capacity = BUSTER_TIER0_VF_LOCALS,
    };
    const uint32_t parameters = kind == 1 ? 3 : kind == 2 ? 18 : kind == 5 ? 7 : 1;
    TypeId types[BUSTER_TIER0_VF_PARAMETERS];
    for (uint32_t parameter = 0; parameter < parameters; parameter += 1)
    {
        const uint32_t class = (kind == 1 && parameter == 2) || (kind == 2 && parameter >= 8) ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_U64;
        tier0_vf_local(stream, class, 0);
        types[parameter] = tier0_program_class_type(class);
    }
    const TypeId results[] =
    {
        tier0_program_class_type(BUSTER_TYPED_CLASS_U64),
        BUSTER_TYPE_VOID,
    };
    const uint32_t counts[] =
    {
        parameters,
        0,
    };
    const uint8_t variadic[] =
    {
        kind != 4,
        0,
    };
    const AbiSignatures signatures =
    {
        .results = results,
        .counts = counts,
        .named = counts,
        .variadic = variadic,
        .count = 2,
        .arguments = types,
    };
    abi_assign(&tables->abi, signatures, stream->parameters, stream->abi);
    bool ready = tier0_vf_expect(tally, stream->abi[0].integer_registers == (kind == 1 ? 2 : kind == 2 || kind == 5 ? 6 : 1) &&
        stream->abi[0].vector_registers == (kind == 1 ? 1 : kind == 2 ? 8 : 0) && stream->abi[0].stack_bytes == (kind == 2 ? 32u : kind == 5 ? 8u : 0u),
        "named-only real ABI batch agrees with independent scalar signature", kind);
    stream->canary[0] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    stream->list[0] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST);
    stream->canary[1] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    stream->list[1] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST);
    stream->canary[2] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    if (kind != 4)
    {
        stream->save_anchor = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA);
        stream->overflow_anchor = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA);
    }
    // An object aligned to 16 or past it, which takes no slot: its place comes after them all.
    stream->object = tier0_vf_local(stream, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN);
    stream->local_sizes[stream->object] = BUSTER_TIER0_VF_OBJECT_BYTES;
    stream->local_alignments[stream->object] = tier0_vf_object_alignments[kind];
    stream->list_pointer[0] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, 0);
    stream->list_pointer[1] = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, 0);
    stream->save_pointer = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, 0);
    stream->overflow_pointer = tier0_vf_local(stream, BUSTER_TYPED_CLASS_U64, 0);
    for (uint32_t list = 0; list < 2; list += 1)
    {
        tier0_vf_address(stream, stream->list[list], 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, stream->list_pointer[list]);
        for (uint32_t word = 0; word < 3; word += 1)
        {
            tier0_vf_address(stream, stream->list[list], word * sizeof(uint64_t));
            tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VF_CANARY ^ (list * 3 + word));
            tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    for (uint32_t canary = 0; canary < 3; canary += 1)
    {
        tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VF_CANARY + canary);
        tier0_vf_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, stream->canary[canary]);
    }
    for (uint32_t word = 0; word < BUSTER_TIER0_VF_OBJECT_WORDS; word += 1)
    {
        tier0_vf_address(stream, stream->object, word * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VF_CANARY ^ (0x100u + word));
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (kind != 4)
    {
        tier0_vf_address(stream, stream->save_anchor, 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, stream->save_pointer);
        tier0_vf_address(stream, stream->overflow_anchor, 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, stream->overflow_pointer);
        const CBuiltinVaLowerCall start =
        {
            .id = kind == 2 ? CBUILTIN_C23_VA_START : CBUILTIN_VA_START,
            .list = stream->list_pointer[0],
            .register_save_area = stream->save_pointer,
            .overflow_arg_area = stream->overflow_pointer,
            .gp_offset = 8u * stream->abi[0].integer_registers,
            .fp_offset = BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * stream->abi[0].vector_registers,
        };
        ready &= tier0_vf_recipe(tally, stream, &start, scalar);
    }
    const CBuiltinVaLowerCall copy =
    {
        .id = CBUILTIN_VA_COPY,
        .list = stream->list_pointer[1],
        .source = stream->list_pointer[0],
    };
    ready &= tier0_vf_recipe(tally, stream, &copy, scalar);
    tier0_vf_object_output(stream, stream->list[0], offsetof(Tier0VfOutput, initial), 3);
    tier0_vf_object_output(stream, stream->list[1], offsetof(Tier0VfOutput, copied), 3);
    if (kind != 4)
    {
        tier0_vf_save_output(stream, offsetof(Tier0VfOutput, save_before));
    }
    tier0_vf_emit(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
    if (kind != 4)
    {
        tier0_vf_save_output(stream, offsetof(Tier0VfOutput, save_after));
    }
    for (uint32_t parameter = 0; parameter < parameters; parameter += 1)
    {
        tier0_vf_output(stream, offsetof(Tier0VfOutput, named) + parameter * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_LOCAL, stream->local_classes[parameter], parameter);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, stream->local_classes[parameter], 0);
    }
    const uint32_t count = tier0_vf_count(kind);
    for (uint32_t value = 0; value < 2 && value < count; value += 1)
    {
        ready &= tier0_vf_argument(tally, stream, stream->list_pointer[1], value, true, scalar);
    }
    for (uint32_t value = 0; value < count / 2; value += 1)
    {
        ready &= tier0_vf_argument(tally, stream, stream->list_pointer[0], value, false, scalar);
    }
    ready &= tier0_vf_recipe(tally, stream, &copy, scalar);
    for (uint32_t value = count / 2; value < count; value += 1)
    {
        ready &= tier0_vf_argument(tally, stream, stream->list_pointer[0], value, false, scalar);
        ready &= tier0_vf_argument(tally, stream, stream->list_pointer[1], value, true, scalar);
    }
    tier0_vf_object_output(stream, stream->list[0], offsetof(Tier0VfOutput, final), 3);
    tier0_vf_object_output(stream, stream->list[1], offsetof(Tier0VfOutput, copy_final), 3);
    for (uint32_t list = 0; list < 2; list += 1)
    {
        tier0_vf_output(stream, offsetof(Tier0VfOutput, addresses) + list * sizeof(uint64_t));
        tier0_vf_address(stream, stream->list[list], 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        const CBuiltinVaLowerCall end =
        {
            .id = CBUILTIN_VA_END,
            .list = stream->list_pointer[list],
        };
        ready &= tier0_vf_recipe(tally, stream, &end, scalar);
    }
    if (kind != 4)
    {
        const uint32_t pointers[] =
        {
            stream->save_pointer,
            stream->overflow_pointer,
        };
        for (uint32_t pointer = 0; pointer < 2; pointer += 1)
        {
            tier0_vf_output(stream, offsetof(Tier0VfOutput, addresses) + (2 + pointer) * sizeof(uint64_t));
            tier0_vf_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, pointers[pointer]);
            tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        }
    }
    for (uint32_t canary = 0; canary < 3; canary += 1)
    {
        tier0_vf_output(stream, offsetof(Tier0VfOutput, addresses) + (4 + canary) * sizeof(uint64_t));
        tier0_vf_address(stream, stream->canary[canary], 0);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
        tier0_vf_output(stream, offsetof(Tier0VfOutput, canaries) + canary * sizeof(uint64_t));
        tier0_vf_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, stream->canary[canary]);
        tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    tier0_vf_output(stream, offsetof(Tier0VfOutput, addresses) + 7 * sizeof(uint64_t));
    tier0_vf_address(stream, stream->object, 0);
    tier0_vf_emit(stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    tier0_vf_object_output(stream, stream->object, offsetof(Tier0VfOutput, object), BUSTER_TIER0_VF_OBJECT_WORDS);
    tier0_vf_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_TIER0_VF_RETURN);
    tier0_vf_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    stream->target = 1;
    stream->function = (Tier0Function)
    {
        .kinds = stream->kinds,
        .classes = stream->classes,
        .values = stream->values,
        .count = stream->buffer.count,
        .local_classes = stream->local_classes,
        .local_flags = stream->local_flags,
        .local_count = stream->buffer.local_count,
        .local_sizes = stream->local_sizes,
        .local_alignments = stream->local_alignments,
        .parameter_count = parameters,
        .parameters = stream->parameters,
        .result = stream->abi[0].result,
        .call_targets = &stream->target,
        .call_first = &stream->call_first,
        .call_argument_counts = &stream->call_arguments,
        .call_variadic = &stream->call_variadic,
        .calls = stream->abi + 1,
        .call_count = 1,
        .incoming_variadic = kind == 4 ? NULL : stream->abi,
    };
    return ready;
}

static bool tier0_vf_same(const Tier0Code* a, const Tier0Code* b)
{
    bool result = a->error == b->error && a->count == b->count && a->branch_count == b->branch_count &&
        a->reference_count == b->reference_count && a->deferred == b->deferred;
    if (result && a->error == TIER0_OK)
    {
        result = memcmp(a->records, b->records, (uint64_t)a->count * sizeof(X86Instruction)) == 0 &&
            memcmp(a->branches, b->branches, (uint64_t)a->branch_count * sizeof(uint32_t)) == 0 &&
            memcmp(a->targets, b->targets, (uint64_t)a->branch_count * sizeof(uint32_t)) == 0;
        for (uint32_t reference = 0; result && reference < a->reference_count; reference += 1)
        {
            const LinkReference* x = a->references + reference;
            const LinkReference* y = b->references + reference;
            result = x->instruction == y->instruction && x->field == y->field && x->kind == y->kind && x->target == y->target && x->addend == y->addend;
        }
    }
    return result;
}

static bool tier0_vf_image(Tier0VfTally* tally, const Tier0Code* code, uint32_t encoder, Tier0VfImage* image)
{
    const X86Target baseline =
    {
        .architecture = X86_ARCH_BASELINE,
    };
    uint8_t allowed[X86_FORM_COUNT];
    x86_target_forms(&baseline, allowed);
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t trampoline = (capacity + 15) & ~UINT64_C(15);
    const uint64_t trampoline_capacity = BUSTER_X86_ENCODER_CAPACITY(2);
    const uint64_t pages = (trampoline + trampoline_capacity + BUSTER_TIER0_VF_PAGE - 1) & ~(uint64_t)(BUSTER_TIER0_VF_PAGE - 1);
    image->mapping_bytes = pages + BUSTER_TIER0_VF_PAGE;
    image->bytes = mmap(NULL, image->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    X86Instruction* records = malloc((uint64_t)code->count * sizeof(*records));
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(*offsets));
    bool ready = tier0_vf_expect(tally, image->bytes != MAP_FAILED && records != NULL && offsets != NULL &&
        code->reference_count == 1 && trampoline + trampoline_capacity < INT32_MAX, "allocate bounded actual-callee image and nearby host trampoline", encoder);
    uint32_t marked = 0;
    for (uint32_t record = 0; ready && record < code->count; record += 1)
    {
        if ((code->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
        {
            ready = tier0_vf_expect(tally, marked == 0 && code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) &&
                code->references[0].instruction == record, "exactly one marked record matches the actual symbolic call", record);
            marked += 1;
        }
    }
    if (ready)
    {
        const BranchSizingCheck check = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ready = tier0_vf_expect(tally, marked == 1 && check.error == BRANCH_SIZING_OK && x86_validate(code->records, code->count) == code->count &&
            x86_target_validate(allowed, code->records, code->count) == code->count,
            "original generated callee has baseline-eligible records and valid branch columns", encoder);
    }
    if (ready)
    {
        memcpy(records, code->records, (uint64_t)code->count * sizeof(*records));
        const BranchSizing sized = encoder == 0 ? size_branches_scalar(records, code->count, code->branches, code->targets, code->branch_count, offsets)
                                                : size_branches(records, code->count, code->branches, code->targets, code->branch_count, offsets);
        uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
        const bool sized_valid = sized.error == BRANCH_SIZING_OK && x86_target_validate(allowed, records, code->count) == code->count;
        const int64_t length = !sized_valid ? -1 : encoder == 0 ? x86_encode_scalar(records, code->count, image->bytes, capacity, NULL, ends)
                                                                                               : x86_encode(records, code->count, image->bytes, capacity, NULL, ends);
        ready = tier0_vf_expect(tally, length > 0 && (uint64_t)length <= capacity, "size branches and encode actual variadic callee", encoder);
        if (ready)
        {
            const LinkReference reference = code->references[0];
            const uint32_t distance = x86_fixup_distance(records[reference.instruction].form, reference.field);
            const int64_t displacement = (int64_t)trampoline - ends[0];
            ready = tier0_vf_expect(tally, reference.kind == BUSTER_LINK_FIXUP_CALL && reference.target == 1 && reference.field == X86_FIXUP_DISPLACEMENT &&
                reference.addend == 0 && distance == 4 && ends[0] >= distance && ends[0] <= (uint64_t)length && displacement >= INT32_MIN && displacement <= INT32_MAX,
                "resolve the actual SymbolId through its bounded rel32 call field", encoder);
            if (ready)
            {
                const int32_t relative = (int32_t)displacement;
                memcpy(image->bytes + ends[0] - distance, &relative, sizeof(relative));
                image->length = (uint32_t)length;
            }
        }
    }
    if (ready)
    {
        const uint64_t address = (uint64_t)(uintptr_t)&tier0_vf_clobber;
        const X86Instruction jump[] =
        {
            {
                .form = X86_FORM_MOV_R64_IMM64,
                .rm = X86_REGISTER_RAX,
                .immediate = (int32_t)address,
                .displacement = (int32_t)(address >> 32),
            },
            {
                .form = X86_FORM_JMP_RM64,
                .rm = X86_REGISTER_RAX,
            },
        };
        const int64_t length = encoder == 0 ? x86_encode_scalar(jump, 2, image->bytes + trampoline, trampoline_capacity, NULL, NULL)
                                            : x86_encode(jump, 2, image->bytes + trampoline, trampoline_capacity, NULL, NULL);
        ready = tier0_vf_expect(tally, x86_validate(jump, 2) == 2 && x86_target_validate(allowed, jump, 2) == 2 && length > 0 && (uint64_t)length <= trampoline_capacity &&
            mprotect(image->bytes, pages, PROT_READ | PROT_EXEC) == 0 && mprotect(image->bytes + pages, BUSTER_TIER0_VF_PAGE, PROT_NONE) == 0,
            "encode nearby trampoline and publish guarded RX native image", encoder);
        image->trampoline = (uint32_t)trampoline;
        image->trampoline_length = length > 0 ? (uint32_t)length : 0;
    }
    free(records);
    free(offsets);
    return ready;
}

static bool tier0_vf_build(Tier0VfTally* tally, const Tier0Tables* tables, Arena* arena, uint32_t kind, Tier0VfNative* native)
{
    const uint64_t mark = arena->length;
    Tier0VfStream streams[2];
    bool ready = true;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        ready &= tier0_vf_fixture(tally, streams + path, tables, kind, path == 0);
    }
    ready &= tier0_vf_expect(tally, streams[0].buffer.count == streams[1].buffer.count && streams[0].buffer.local_count == streams[1].buffer.local_count &&
        memcmp(streams[0].kinds, streams[1].kinds, streams[0].buffer.count) == 0 && memcmp(streams[0].classes, streams[1].classes, streams[0].buffer.count) == 0 &&
        memcmp(streams[0].values, streams[1].values, (uint64_t)streams[0].buffer.count * sizeof(uint64_t)) == 0 &&
        memcmp(streams[0].local_classes, streams[1].local_classes, streams[0].buffer.local_count) == 0 &&
        memcmp(streams[0].local_flags, streams[1].local_flags, streams[0].buffer.local_count) == 0,
        "both varargs recipe publishers make the same original callee", kind);
    Tier0Code code[2] = { 0 };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const Tier0Check check = tier0_validate(&streams[path].function);
        ready = tier0_vf_expect(tally, check.error == TIER0_OK, "incoming metadata and address-only special locals validate", kind * 2 + path);
        if (ready)
        {
            code[path] = path == 0 ? tier0_compile_scalar(&streams[path].function, arena) : tier0_compile(&streams[path].function, arena);
            ready = tier0_vf_expect(tally, code[path].error == TIER0_OK, "compile actual incoming variadic frame", kind * 2 + path);
        }
    }
    if (ready)
    {
        ready = tier0_vf_expect(tally, tier0_vf_same(code, code + 1), "scalar and vector callees have identical complete logical output", kind);
    }
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t encoder = 0; ready && encoder < 2; encoder += 1)
        {
            ready = tier0_vf_image(tally, code + path, encoder, &native->images[path][encoder]);
            const Tier0VfImage* first = &native->images[0][0];
            const Tier0VfImage* image = &native->images[path][encoder];
            if (ready)
            {
                ready = tier0_vf_expect(tally, first->length == image->length && first->trampoline_length == image->trampoline_length &&
                    memcmp(first->bytes, image->bytes, image->length) == 0 &&
                    memcmp(first->bytes + first->trampoline, image->bytes + image->trampoline, image->trampoline_length) == 0,
                    "all four separately encoded executable images agree exactly", kind * 4 + path * 2 + encoder);
            }
        }
    }
    arena_pop_to(arena, mark);
    return ready;
}

static uint64_t tier0_vf_gp(uint32_t index)
{
    const uint64_t result = UINT64_C(0x9d17e2a34865b0fc) ^ (UINT64_C(0x0123456789abcdef) * (index + 1));
    return result;
}

static double tier0_vf_fp(uint32_t index)
{
    static const double values[] =
    {
        -0.0,
        1.25,
        -2.5,
        3.75,
        -4.125,
        5.5,
        -6.75,
        7.875,
        -8.25,
        9.625,
    };
    const double result = values[index % BUSTER_ARRAY_LENGTH(values)];
    return result;
}

static uint64_t tier0_vf_fp_bits(uint32_t index)
{
    const double value = tier0_vf_fp(index);
    uint64_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static __m128i tier0_vf_vector(uint32_t index)
{
    const __m128i result = _mm_set_epi64x((int64_t)tier0_vf_gp(index + 20), (int64_t)tier0_vf_gp(index + 10));
    return result;
}

static __attribute__((no_sanitize("function"))) uint64_t tier0_vf_invoke(const Tier0VfImage* image, Tier0VfOutput* output, uint32_t kind)
{
    uint64_t result;
    if (kind == 0)
    {
        uint64_t (*call)(Tier0VfOutput*, ...) = (uint64_t (*)(Tier0VfOutput*, ...))(void*)image->bytes;
        result = call(output, tier0_vf_gp(0), tier0_vf_gp(1), tier0_vf_gp(2), tier0_vf_gp(3), tier0_vf_gp(4),
            tier0_vf_gp(5), tier0_vf_gp(6), tier0_vf_gp(7), tier0_vf_gp(8), tier0_vf_gp(9));
    }
    else if (kind == 1)
    {
        uint64_t (*call)(Tier0VfOutput*, uint64_t, double, ...) = (uint64_t (*)(Tier0VfOutput*, uint64_t, double, ...))(void*)image->bytes;
        result = call(output, tier0_vf_gp(30), tier0_vf_fp(9), tier0_vf_gp(0), tier0_vf_fp(0), tier0_vf_gp(1), tier0_vf_fp(1),
            tier0_vf_gp(2), tier0_vf_fp(2), tier0_vf_gp(3), tier0_vf_fp(3), tier0_vf_gp(4), tier0_vf_fp(4), tier0_vf_gp(5), tier0_vf_fp(5),
            tier0_vf_gp(6), tier0_vf_fp(6), tier0_vf_gp(7), tier0_vf_fp(7), tier0_vf_fp(8), tier0_vf_fp(9));
    }
    else if (kind == 2)
    {
        uint64_t (*call)(Tier0VfOutput*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
            double, double, double, double, double, double, double, double, double, double, ...) =
            (uint64_t (*)(Tier0VfOutput*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                double, double, double, double, double, double, double, double, double, double, ...))(void*)image->bytes;
        result = call(output, tier0_vf_gp(30), tier0_vf_gp(31), tier0_vf_gp(32), tier0_vf_gp(33), tier0_vf_gp(34), tier0_vf_gp(35), tier0_vf_gp(36),
            tier0_vf_fp(0), tier0_vf_fp(1), tier0_vf_fp(2), tier0_vf_fp(3), tier0_vf_fp(4), tier0_vf_fp(5), tier0_vf_fp(6), tier0_vf_fp(7), tier0_vf_fp(8), tier0_vf_fp(9),
            tier0_vf_gp(0), tier0_vf_fp(0), tier0_vf_gp(1), tier0_vf_fp(1), tier0_vf_gp(2), tier0_vf_fp(2), tier0_vf_gp(3), tier0_vf_fp(3));
    }
    else if (kind == 3)
    {
        uint64_t (*call)(Tier0VfOutput*, ...) = (uint64_t (*)(Tier0VfOutput*, ...))(void*)image->bytes;
        result = call(output, tier0_vf_gp(0), tier0_vf_vector(0), tier0_vf_gp(1), tier0_vf_vector(1), tier0_vf_gp(2), tier0_vf_vector(2),
            tier0_vf_gp(3), tier0_vf_vector(3), tier0_vf_gp(4), tier0_vf_vector(4), tier0_vf_vector(5), tier0_vf_vector(6), tier0_vf_vector(7));
    }
    else if (kind == 5)
    {
        uint64_t (*call)(Tier0VfOutput*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, ...) =
            (uint64_t (*)(Tier0VfOutput*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, ...))(void*)image->bytes;
        result = call(output, tier0_vf_gp(30), tier0_vf_gp(31), tier0_vf_gp(32), tier0_vf_gp(33), tier0_vf_gp(34), tier0_vf_gp(35),
            tier0_vf_gp(0), tier0_vf_fp(0), tier0_vf_gp(1), tier0_vf_fp(1), tier0_vf_gp(2), tier0_vf_fp(2), tier0_vf_gp(3), tier0_vf_fp(3));
    }
    else
    {
        uint64_t (*call)(Tier0VfOutput*) = (uint64_t (*)(Tier0VfOutput*))(void*)image->bytes;
        result = call(output);
    }
    return result;
}

static bool tier0_vf_disjoint(uint64_t first, uint64_t first_bytes, uint64_t second, uint64_t second_bytes)
{
    const bool result = first <= second ? second - first >= first_bytes : first - second >= second_bytes;
    return result;
}

static void tier0_vf_observe(Tier0VfTally* tally, const Tier0VfOutput* output, uint32_t kind)
{
    const uint64_t untouched = UINT64_C(0xa5a5a5a5a5a5a5a5);
    const uint32_t count = tier0_vf_count(kind);
    const uint32_t parameters = kind == 1 ? 3 : kind == 2 ? 18 : kind == 5 ? 7 : 1;
    for (uint32_t parameter = 0; parameter < BUSTER_TIER0_VF_PARAMETERS; parameter += 1)
    {
        const uint64_t expected = parameter == 0 ? (uint64_t)(uintptr_t)output : parameter >= parameters ? untouched :
            kind == 1 ? parameter == 1 ? tier0_vf_gp(30) : tier0_vf_fp_bits(9) : parameter < 8 ? tier0_vf_gp(parameter + 29) : tier0_vf_fp_bits(parameter - 8);
        tier0_vf_expect(tally, output->named[parameter] == expected, "named GP/FP and incoming-stack parameters survive intervening call", kind * 100 + parameter);
    }
    uint32_t general = 0;
    uint32_t floating = 0;
    for (uint32_t value = 0; value < BUSTER_TIER0_VF_VALUES; value += 1)
    {
        uint64_t expected = untouched;
        if (value < count)
        {
            expected = tier0_vf_class(kind, value) == BUSTER_TYPED_CLASS_U64 ? tier0_vf_gp(general++) : tier0_vf_fp_bits(floating++);
        }
        const bool copied = value < count && (value < 2 || value >= count / 2);
        tier0_vf_expect(tally, output->values[value] == expected && output->copy_values[value] == (copied ? expected : untouched),
            "independent original and copied cursors preserve every scalar argument bit and unused output", kind * 100 + value);
    }
    for (uint32_t canary = 0; canary < 3; canary += 1)
    {
        tier0_vf_expect(tally, output->canaries[canary] == BUSTER_TIER0_VF_CANARY + canary,
            "24-byte list stores preserve surrounding live frame objects", kind * 10 + canary);
    }
    tier0_vf_expect(tally, output->addresses[0] == output->addresses[4] + 8 && output->addresses[5] == output->addresses[0] + 24 &&
        output->addresses[1] == output->addresses[5] + 8 && output->addresses[6] == output->addresses[1] + 24,
        "consecutive ordinary and list objects allocate exactly eight and twenty-four bytes", kind);
    for (uint32_t first = 0; first < 7; first += 1)
    {
        if (kind != 4 || (first != 2 && first != 3))
        {
            tier0_vf_expect(tally, (output->addresses[first] & 7) == 0, "list, frame anchor and scalar addresses are naturally aligned", first);
        }
        for (uint32_t second = first + 1; second < 7; second += 1)
        {
            const bool private_first = first < 2 || first >= 4;
            const bool private_second = second < 2 || second >= 4;
            if (private_first && private_second)
            {
                tier0_vf_expect(tally, tier0_vf_disjoint(output->addresses[first], first < 2 ? 24 : 8, output->addresses[second], second < 2 ? 24 : 8),
                    "every explicit list owns 24 disjoint bytes", first * 10 + second);
            }
        }
        if (kind != 4 && (first < 2 || first >= 4))
        {
            tier0_vf_expect(tally, tier0_vf_disjoint(output->addresses[first], first < 2 ? 24 : 8, output->addresses[2], BUSTER_ABI_SAVE_AREA_BYTES),
                "incoming save area is disjoint from every live private object", first);
        }
    }
    // The object: its pattern, at a multiple of its alignment, apart from the lists (addresses 0 and 1), the canaries (4 to
    // 6) and, with incoming metadata, the save area (2), and below the overflow area (3), whose anchor counts the whole
    // frame.
    for (uint32_t word = 0; word < BUSTER_TIER0_VF_OBJECT_WORDS; word += 1)
    {
        tier0_vf_expect(tally, output->object[word] == (BUSTER_TIER0_VF_CANARY ^ (0x100u + word)),
            "an object aligned to 16 or past it keeps its bytes beside the lists, the save area and the intervening call", kind * 10 + word);
    }
    tier0_vf_expect(tally, (output->addresses[7] & ((UINT64_C(1) << tier0_vf_object_alignments[kind]) - 1)) == 0,
        "an object aligned to 16 or past it is at a multiple of its alignment in the frame", kind);
    for (uint32_t other = 0; other < 7; other += 1)
    {
        const uint64_t other_bytes = other < 2 ? 24 : other == 2 ? BUSTER_ABI_SAVE_AREA_BYTES : 8;
        if (other != 3 && (kind != 4 || other != 2))
        {
            tier0_vf_expect(tally, tier0_vf_disjoint(output->addresses[7], BUSTER_TIER0_VF_OBJECT_BYTES, output->addresses[other], other_bytes),
                "an object aligned to 16 or past it owns its bytes apart from the lists, the canaries and the save area", kind * 10 + other);
        }
    }
    if (kind != 4)
    {
        tier0_vf_expect(tally, output->addresses[7] + BUSTER_TIER0_VF_OBJECT_BYTES <= output->addresses[3],
            "an object in the frame lies below the incoming overflow area", kind);
    }
    if (kind == 4)
    {
        for (uint32_t word = 0; word < 3; word += 1)
        {
            const uint64_t expected = BUSTER_TIER0_VF_CANARY ^ word;
            tier0_vf_expect(tally, output->initial[word] == expected && output->copied[word] == expected && output->final[word] == expected && output->copy_final[word] == expected,
                "ordinary null-metadata callee has two complete 24-byte copy objects", word);
        }
    }
    else
    {
        AbiVaList initial;
        AbiVaList final;
        memcpy(&initial, output->initial, sizeof(initial));
        memcpy(&final, output->final, sizeof(final));
        const uint32_t named_gp = kind == 1 ? 2 : kind == 2 || kind == 5 ? 6 : 1;
        const uint32_t named_fp = kind == 1 ? 1 : kind == 2 ? 8 : 0;
        const uint32_t remaining_gp = BUSTER_ABI_INTEGER_ARGUMENTS - named_gp;
        const uint32_t remaining_fp = BUSTER_ABI_VECTOR_ARGUMENTS - named_fp;
        const uint32_t used_gp = general < remaining_gp ? general : remaining_gp;
        const uint32_t used_fp = floating < remaining_fp ? floating : remaining_fp;
        const uint32_t overflow = general - used_gp + floating - used_fp;
        tier0_vf_expect(tally, initial.gp_offset == 8 * named_gp && initial.fp_offset == 48 + 16 * named_fp &&
            initial.reg_save_area == output->addresses[2] && initial.overflow_arg_area == output->addresses[3] &&
            memcmp(output->initial, output->copied, sizeof(initial)) == 0,
            "actual va_start and initial va_copy expose named-only offsets and generated frame anchors", kind);
        tier0_vf_expect(tally, final.gp_offset == 8 * (named_gp + used_gp) && final.fp_offset == 48 + 16 * (named_fp + used_fp) &&
            final.reg_save_area == initial.reg_save_area && final.overflow_arg_area == initial.overflow_arg_area + 8 * overflow &&
            memcmp(output->final, output->copy_final, sizeof(final)) == 0,
            "exhausted independent banks share the exact incoming overflow cursor after copied traversal", kind);
        tier0_vf_expect(tally, memcmp(output->save_before, output->save_after, BUSTER_ABI_SAVE_AREA_BYTES) == 0,
            "all 176 incoming save bytes survive list objects, recipe temporaries and actual intervening call", kind);
        tier0_vf_expect(tally, output->save_before[0] == (uint64_t)(uintptr_t)output, "named output pointer saved before parameter homes move", kind);
        for (uint32_t slot = 1; slot < 6; slot += 1)
        {
            const uint64_t expected = kind == 1 ? slot == 1 ? tier0_vf_gp(30) : tier0_vf_gp(slot - 2) : kind == 2 || kind == 5 ? tier0_vf_gp(slot + 29) : tier0_vf_gp(slot - 1);
            tier0_vf_expect(tally, output->save_before[slot] == expected, "every incoming GP slot retains its original named/unnamed value", kind * 10 + slot);
        }
        if (kind == 1 || kind == 2 || kind == 3 || kind == 5)
        {
            for (uint32_t slot = 0; slot < (kind == 5 ? 4u : 8u); slot += 1)
            {
                const uint64_t expected = kind == 3 ? tier0_vf_gp(slot + 10) : kind == 1 && slot == 0 ? tier0_vf_fp_bits(9) : tier0_vf_fp_bits(slot - (kind == 1));
                tier0_vf_expect(tally, output->save_before[6 + 2 * slot] == expected, "all eight incoming vector low halves saved before homes and clobbers", kind * 10 + slot);
                if (kind == 3)
                {
                    tier0_vf_expect(tally, output->save_before[7 + 2 * slot] == tier0_vf_gp(slot + 20),
                        "every full 128-bit incoming XMM register is saved, including its independent high half", slot);
                }
            }
        }
    }
}

static void tier0_vf_native(Tier0VfTally* tally, const Tier0VfNative natives[BUSTER_TIER0_VF_FIXTURES])
{
    _Static_assert(sizeof(Tier0VfOutput) + 32 < BUSTER_TIER0_VF_PAGE, "guarded native output fits one page");
    uint8_t* mapping = mmap(NULL, 3 * BUSTER_TIER0_VF_PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + BUSTER_TIER0_VF_PAGE, BUSTER_TIER0_VF_PAGE, PROT_READ | PROT_WRITE) == 0;
    if (tier0_vf_expect(tally, ready, "guarded native output page", 0))
    {
        uint8_t* page = mapping + BUSTER_TIER0_VF_PAGE;
        for (uint32_t kind = 0; kind < BUSTER_TIER0_VF_FIXTURES; kind += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t encoder = 0; encoder < 2; encoder += 1)
                {
                    for (uint32_t edge = 0; edge < 2; edge += 1)
                    {
                        memset(page, 0xa5, BUSTER_TIER0_VF_PAGE);
                        const uint32_t offset = edge == 0 ? 0 : BUSTER_TIER0_VF_PAGE - sizeof(Tier0VfOutput);
                        Tier0VfOutput* output = (Tier0VfOutput*)(void*)(page + offset);
                        const uint64_t before = tier0_vf_clobber_calls;
                        const uint64_t result = tier0_vf_invoke(&natives[kind].images[path][encoder], output, kind);
                        tier0_vf_expect(tally, result == BUSTER_TIER0_VF_RETURN && tier0_vf_clobber_calls == before + 1,
                            "Clang caller enters generated callee and returns through a real intervening native call", kind * 100 + path * 10 + encoder * 2 + edge);
                        tier0_vf_observe(tally, output, kind);
                        bool untouched = true;
                        for (uint32_t byte = 0; byte < BUSTER_TIER0_VF_PAGE; byte += 1)
                        {
                            if (byte < offset || byte >= offset + sizeof(*output))
                            {
                                untouched &= page[byte] == 0xa5;
                            }
                        }
                        tier0_vf_expect(tally, untouched, "native output stores respect both exact page-edge extents", kind);
                        tally->native_calls += 1;
                    }
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 3 * BUSTER_TIER0_VF_PAGE);
    }
}

static void tier0_vf_null(Tier0VfTally* tally, Arena* arena)
{
    const uint8_t kinds[] =
    {
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_VOID,
    };
    const uint64_t values[] =
    {
        0,
    };
    const Tier0Function omitted =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 1,
    };
    Tier0Function explicit_null = omitted;
    explicit_null.incoming_variadic = NULL;
    const uint64_t mark = arena->length;
    const Tier0Code scalar = tier0_compile_scalar(&omitted, arena);
    const Tier0Code vector = tier0_compile(&explicit_null, arena);
    const X86Instruction expected =
    {
        .form = X86_FORM_RET,
    };
    bool ready = tier0_vf_expect(tally, tier0_validate(&omitted).error == TIER0_OK && tier0_vf_same(&scalar, &vector) && scalar.error == TIER0_OK &&
        scalar.count == 1 && scalar.branch_count == 0 && scalar.reference_count == 0 && memcmp(scalar.records, &expected, sizeof(expected)) == 0,
        "omitted and explicit null incoming metadata preserve the original RET-only record stream", 0);
    if (ready)
    {
        uint8_t bytes[2][BUSTER_X86_ENCODER_CAPACITY(1)] = { 0 };
        const int64_t a = x86_encode_scalar(scalar.records, 1, bytes[0], sizeof(bytes[0]), NULL, NULL);
        const int64_t b = x86_encode(vector.records, 1, bytes[1], sizeof(bytes[1]), NULL, NULL);
        ready = tier0_vf_expect(tally, a == 1 && b == 1 && bytes[0][0] == 0xc3 && bytes[1][0] == 0xc3,
            "default ordinary frame remains exactly its independent original C3 byte", 0);
    }
    // Independently specified old ordinary frame: reserve one eight-byte address-taken local, form its address,
    // discard it and release the frame. No incoming metadata, call alignment, save area or parameter is involved.
    const uint8_t frame_kinds[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t frame_classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
    };
    const uint64_t frame_values[] =
    {
        0,
        0,
        0,
    };
    const uint8_t local_class = BUSTER_TYPED_CLASS_U64;
    const uint8_t local_flags = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
    const Tier0Function ordinary =
    {
        .kinds = frame_kinds,
        .classes = frame_classes,
        .values = frame_values,
        .count = 3,
        .local_classes = &local_class,
        .local_flags = &local_flags,
        .local_count = 1,
    };
    const X86Instruction frame_records[] =
    {
        {
            .form = X86_FORM_SUB_RM64_IMM8,
            .rm = X86_REGISTER_RSP,
            .immediate = 8,
        },
        {
            .form = X86_FORM_LEA_R64_M,
            .reg = X86_REGISTER_RAX,
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_MEMORY_BASE,
        },
        {
            .form = X86_FORM_ADD_RM64_IMM8,
            .rm = X86_REGISTER_RSP,
            .immediate = 8,
        },
        {
            .form = X86_FORM_RET,
        },
    };
    const uint8_t frame_bytes[] =
    {
        0x48, 0x83, 0xec, 0x08,
        0x48, 0x8d, 0x04, 0x24,
        0x48, 0x83, 0xc4, 0x08,
        0xc3,
    };
    Tier0Function ordinary_null = ordinary;
    ordinary_null.incoming_variadic = NULL;
    const Tier0Code ordinary_scalar = tier0_compile_scalar(&ordinary, arena);
    const Tier0Code ordinary_vector = tier0_compile(&ordinary_null, arena);
    ready = tier0_vf_expect(tally, tier0_validate(&ordinary).error == TIER0_OK && tier0_vf_same(&ordinary_scalar, &ordinary_vector) &&
        ordinary_scalar.error == TIER0_OK && ordinary_scalar.count == BUSTER_ARRAY_LENGTH(frame_records) && ordinary_scalar.branch_count == 0 &&
        ordinary_scalar.reference_count == 0 && memcmp(ordinary_scalar.records, frame_records, sizeof(frame_records)) == 0,
        "ordinary eight-byte memory-local frame retains independently specified old records under default/null metadata", 0);
    if (ready)
    {
        uint8_t bytes[2][BUSTER_X86_ENCODER_CAPACITY(BUSTER_ARRAY_LENGTH(frame_records))] = { 0 };
        const int64_t a = x86_encode_scalar(ordinary_scalar.records, ordinary_scalar.count, bytes[0], sizeof(bytes[0]), NULL, NULL);
        const int64_t b = x86_encode(ordinary_vector.records, ordinary_vector.count, bytes[1], sizeof(bytes[1]), NULL, NULL);
        tier0_vf_expect(tally, a == sizeof(frame_bytes) && b == sizeof(frame_bytes) &&
            memcmp(bytes[0], frame_bytes, sizeof(frame_bytes)) == 0 && memcmp(bytes[1], frame_bytes, sizeof(frame_bytes)) == 0,
            "ordinary null-metadata frame preserves the exact independent thirteen-byte baseline encoding", 0);
    }
    arena_pop_to(arena, mark);
}

static void tier0_vf_invalid(Tier0VfTally* tally, Arena* arena)
{
    const uint8_t kinds[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
    };
    const uint64_t values[] =
    {
        0,
        0,
        0,
    };
    uint8_t local_class = BUSTER_TYPED_CLASS_U64;
    uint8_t local_flags = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST;
    AbiCall incoming =
    {
        .stack_alignment_log2 = 4,
    };
    Tier0Function function =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 3,
        .local_classes = &local_class,
        .local_flags = &local_flags,
        .local_count = 1,
        .incoming_variadic = &incoming,
    };
    tier0_vf_expect(tally, tier0_validate(&function).error == TIER0_OK, "independent minimal special-local control validates", 0);
    for (uint32_t variant = 0; variant < 6; variant += 1)
    {
        AbiCall bad = incoming;
        if (variant == 0)
        {
            bad.integer_registers = 7;
        }
        else if (variant == 1)
        {
            bad.vector_registers = 9;
        }
        else if (variant == 2)
        {
            bad.stack_bytes = 1;
        }
        else if (variant == 3)
        {
            bad.stack_alignment_log2 = 3;
        }
        else if (variant == 4)
        {
            bad.stack_bytes = UINT32_MAX & ~7u;
        }
        else
        {
            bad.stack_bytes = INT32_MAX & ~7u;
        }
        function.incoming_variadic = &bad;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const uint64_t mark = arena->length;
            const Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
            tier0_vf_expect(tally, code.error == TIER0_ERROR_UNSUPPORTED && arena->length == mark,
                "invalid incoming ABI bounds reject both compilation paths transactionally", variant * 2 + path);
        }
    }
    function.incoming_variadic = &incoming;
    for (uint32_t variant = 0; variant < 6; variant += 1)
    {
        local_class = variant == 2 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
        local_flags = variant == 0 ? BUSTER_TYPED_LOCAL_VA_LIST : BUSTER_TYPED_LOCAL_ADDRESS_TAKEN |
            (variant == 1 ? BUSTER_TYPED_LOCAL_VA_LIST | BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA : variant == 3 ? BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA : BUSTER_TYPED_LOCAL_VA_LIST);
        function.incoming_variadic = variant == 3 ? NULL : &incoming;
        AbiLocation parameter =
        {
            .registers =
            {
                BUSTER_ABI_RDI,
                BUSTER_ABI_REGISTER_NONE,
            },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        };
        function.parameter_count = variant == 4;
        function.parameters = &parameter;
        uint8_t changed_kinds[3];
        memcpy(changed_kinds, kinds, sizeof(changed_kinds));
        changed_kinds[0] = variant == 5 ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_LOCAL_ADDRESS;
        function.kinds = changed_kinds;
        tier0_vf_expect(tally, tier0_validate(&function).error != TIER0_OK,
            "missing address flag, mixed special flags, wrong class, absent incoming metadata, parameters and value reads reject", variant);
    }
    function.kinds = kinds;
    function.parameter_count = 0;
    function.parameters = NULL;
    function.incoming_variadic = &incoming;
    local_class = BUSTER_TYPED_CLASS_U64;
    local_flags = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST;
    const uint8_t stores[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_RETURN,
    };
    function.kinds = stores;
    tier0_vf_expect(tally, tier0_validate(&function).error != TIER0_OK, "value stores cannot overwrite an address-only list object", 0);
    // A genuinely scalar seven-GP named signature has exactly one incoming eight-byte stack slot. Test its
    // metadata/location disagreement separately from the frame-size overflow above; no count is inferred anew.
    uint8_t named_classes[7];
    uint8_t named_flags[7] = { 0 };
    AbiLocation locations[7] = { 0 };
    memset(named_classes, BUSTER_TYPED_CLASS_U64, sizeof(named_classes));
    for (uint32_t parameter = 0; parameter < 7; parameter += 1)
    {
        locations[parameter].kind = parameter < 6 ? BUSTER_ABI_LOCATION_REGISTERS : BUSTER_ABI_LOCATION_STACK;
        locations[parameter].registers[0] = parameter < 6 ? abi_integer_arguments[parameter] : BUSTER_ABI_REGISTER_NONE;
        locations[parameter].registers[1] = BUSTER_ABI_REGISTER_NONE;
    }
    incoming.integer_registers = 6;
    incoming.stack_bytes = 8;
    function.kinds = kinds + 2;
    function.classes = classes + 2;
    function.values = values + 2;
    function.count = 1;
    function.local_classes = named_classes;
    function.local_flags = named_flags;
    function.local_count = 7;
    function.parameter_count = 7;
    function.parameters = locations;
    tier0_vf_expect(tally, tier0_validate(&function).error == TIER0_OK, "seven named GP parameters have exactly one real eight-byte stack slot", 0);
    for (uint32_t variant = 0; variant < 3; variant += 1)
    {
        incoming.stack_bytes = variant == 0 ? 0 : 8;
        locations[6].stack_offset = variant == 1 ? 1 : variant == 2 ? 8 : 0;
        tier0_vf_expect(tally, tier0_validate(&function).error != TIER0_OK,
            "incoming stack parameter metadata rejects omitted, unaligned and out-of-bounds named slots", variant);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const uint64_t mark = arena->length;
            const Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
            tier0_vf_expect(tally, code.error == TIER0_ERROR_UNSUPPORTED && arena->length == mark,
                "both compilers refuse inconsistent named-stack metadata without publishing output", variant * 2 + path);
        }
    }
}

static void tier0_vf_guards(Tier0VfTally* tally, Arena* arena)
{
    uint8_t* mappings[6];
    bool ready = true;
    for (uint32_t view = 0; view < 6; view += 1)
    {
        mappings[view] = mmap(NULL, 2 * BUSTER_TIER0_VF_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[view] != MAP_FAILED;
        if (mappings[view] != MAP_FAILED)
        {
            ready &= mprotect(mappings[view] + BUSTER_TIER0_VF_PAGE, BUSTER_TIER0_VF_PAGE, PROT_NONE) == 0;
        }
    }
    if (tier0_vf_expect(tally, ready, "guarded exact incoming metadata, source and local columns", 0))
    {
        uint8_t* kinds = mappings[0] + BUSTER_TIER0_VF_PAGE - 7;
        uint8_t* classes = mappings[1] + BUSTER_TIER0_VF_PAGE - 7;
        uint64_t* values = (uint64_t*)(void*)(mappings[2] + BUSTER_TIER0_VF_PAGE - 7 * sizeof(uint64_t));
        uint8_t* local_classes = mappings[3] + BUSTER_TIER0_VF_PAGE - 3;
        uint8_t* local_flags = mappings[4] + BUSTER_TIER0_VF_PAGE - 3;
        AbiCall* incoming = (AbiCall*)(void*)(mappings[5] + BUSTER_TIER0_VF_PAGE - sizeof(AbiCall));
        const uint8_t flags[] =
        {
            BUSTER_TYPED_LOCAL_VA_LIST,
            BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA,
            BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA,
        };
        for (uint32_t local = 0; local < 3; local += 1)
        {
            kinds[2 * local] = BUSTER_TYPED_LOCAL_ADDRESS;
            kinds[2 * local + 1] = BUSTER_TYPED_DISCARD;
            classes[2 * local] = BUSTER_TYPED_CLASS_U64;
            classes[2 * local + 1] = BUSTER_TYPED_CLASS_U64;
            values[2 * local] = local;
            values[2 * local + 1] = 0;
            local_classes[local] = BUSTER_TYPED_CLASS_U64;
            local_flags[local] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | flags[local];
        }
        kinds[6] = BUSTER_TYPED_RETURN;
        classes[6] = BUSTER_TYPED_CLASS_VOID;
        values[6] = 0;
        *incoming = (AbiCall)
        {
            .stack_alignment_log2 = 4,
        };
        const Tier0Function function =
        {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = 7,
            .local_classes = local_classes,
            .local_flags = local_flags,
            .local_count = 3,
            .incoming_variadic = incoming,
        };
        const uint64_t mark = arena->length;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = check.error == TIER0_OK ? tier0_compile_scalar(&function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
        const Tier0Code vector = check.error == TIER0_OK ? tier0_compile(&function, arena) : (Tier0Code){ .error = TIER0_ERROR_INVALID };
        tier0_vf_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_vf_same(&scalar, &vector),
            "both paths read only exact logical metadata, entry and local slices", 0);
        arena_pop_to(arena, mark);
        uint8_t bytes[128];
        memset(bytes, 0xa5, sizeof(bytes));
        for (uint32_t path = 0; path < 2; path += 1)
        {
            Arena empty =
            {
                .base = bytes,
                .capacity = sizeof(bytes),
                .length = sizeof(bytes),
            };
            const Tier0Code code = path == 0 ? tier0_compile_scalar(&function, &empty) : tier0_compile(&function, &empty);
            bool unchanged = true;
            for (uint32_t byte = 0; byte < sizeof(bytes); byte += 1)
            {
                unchanged &= bytes[byte] == 0xa5;
            }
            tier0_vf_expect(tally, code.error == TIER0_ERROR_MEMORY && empty.length == sizeof(bytes) && unchanged,
                "incoming frame output exhaustion preserves prior arena length and bytes", path);
        }
    }
    for (uint32_t view = 0; view < 6; view += 1)
    {
        if (mappings[view] != MAP_FAILED)
        {
            munmap(mappings[view], 2 * BUSTER_TIER0_VF_PAGE);
        }
    }
}

static int test_tier0_variadic_frame(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: build/compiler test-tier0-variadic-frame\n");
    }
    else
    {
        Tier0VfTally tally = { 0 };
        Tier0Tables tables = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 26);
        bool ready = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && tier0_tables_open(&tables);
        ready = tier0_vf_expect(&tally, ready && arena.base != NULL, "initialize real ABI and Tier0 tables", 0);
        Tier0VfNative natives[BUSTER_TIER0_VF_FIXTURES] = { 0 };
        if (ready)
        {
            tier0_vf_null(&tally, &arena);
            tier0_vf_invalid(&tally, &arena);
            tier0_vf_guards(&tally, &arena);
        }
        for (uint32_t kind = 0; ready && kind < BUSTER_TIER0_VF_FIXTURES; kind += 1)
        {
            ready = tier0_vf_build(&tally, &tables, &arena, kind, natives + kind);
        }
        Tier0VfTally* shared = MAP_FAILED;
        if (ready)
        {
            shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            ready = tier0_vf_expect(&tally, shared != MAP_FAILED, "native child tally", 0);
        }
        if (ready)
        {
            fflush(NULL);
            const pid_t child = fork();
            if (child == 0)
            {
                const struct rlimit no_core = { 0 };
                sigset_t unblocked;
                sigemptyset(&unblocked);
                if (signal(SIGALRM, SIG_DFL) == SIG_ERR || sigprocmask(SIG_SETMASK, &unblocked, NULL) != 0 || setrlimit(RLIMIT_CORE, &no_core) != 0)
                {
                    _exit(98);
                }
                alarm(15);
                tier0_vf_native(shared, natives);
                _exit(shared->failures == 0 ? 0 : 1);
            }
            int status = 0;
            pid_t waited;
            do
            {
                waited = child > 0 ? waitpid(child, &status, 0) : -1;
            } while (child > 0 && waited < 0 && errno == EINTR);
            tier0_vf_expect(&tally, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "bounded independent Clang-to-generated-callee native cases complete", (uint32_t)status);
        }
        if (shared != MAP_FAILED)
        {
            tally.checks += shared->checks;
            tally.failures += shared->failures;
            tally.native_calls += shared->native_calls;
            munmap(shared, sizeof(*shared));
        }
        tier0_vf_expect(&tally, tally.native_calls == BUSTER_TIER0_VF_FIXTURES * 2 * 2 * 2,
            "all six original fixtures, both Tier0 paths, both encoders and both output boundaries execute", 0);
        if (ready)
        {
            tier0_vf_null(&tally, &arena);
        }
        for (uint32_t kind = 0; kind < BUSTER_TIER0_VF_FIXTURES; kind += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t encoder = 0; encoder < 2; encoder += 1)
                {
                    Tier0VfImage* image = &natives[kind].images[path][encoder];
                    if (image->bytes != NULL && image->bytes != MAP_FAILED)
                    {
                        munmap(image->bytes, image->mapping_bytes);
                    }
                }
            }
        }
        tier0_tables_close(&tables);
        arena_release(&arena);
        printf("test-tier0-variadic-frame: %" PRIu64 " checks, %" PRIu64 " failures; native=%" PRIu64 "\n", tally.checks, tally.failures, tally.native_calls);
        result = tally.failures != 0;
    }
    return result;
}
