// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Execute the frame planner's native consumer on a controlled stack. The generated caller sets callee-save sentinels,
// six integer arguments, eight full-width vector arguments, and AL. The callee checks actual object addresses and
// values, moves a dynamic RSP, deliberately changes every allocated callee-save register, and returns through the
// emitted epilogue. Zeroed vector save bytes distinguish AL == 0 from AL != 0. Guard-page probes have a no-probe
// control: only the probing function must fault. Every executed byte comes from this bounded test's own records.

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_TIER1_FRAME_EXEC_CODE_BYTES (64u * 1024u)
#define BUSTER_TIER1_FRAME_EXEC_STACK_BYTES (512u * 1024u)
#define BUSTER_TIER1_FRAME_EXEC_RECORDS 1024u

typedef struct Tier1FrameExecutionTest Tier1FrameExecutionTest;
struct Tier1FrameExecutionTest
{
    uint64_t checks;
    uint64_t failures;
    uint8_t* allowed;
    uint8_t* code;
    uint8_t* stack;
    uint64_t vectors[16];
    Arena output;
    Arena scratch;
};

static void tier1_frame_execution_expect(Tier1FrameExecutionTest* test, bool passed, const char* message)
{
    test->checks += 1;
    if (!passed)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "test-tier1-frame-execution: %s\n", message);
        }
        test->failures += 1;
    }
}

static X86Instruction tier1_frame_execution_constant(uint32_t reg, uint64_t value)
{
    const X86Instruction record = {
        .form = X86_FORM_MOV_R64_IMM64,
        .rm = (uint8_t)reg,
        .immediate = (int32_t)value,
        .displacement = (int32_t)(value >> 32),
    };
    return record;
}

static uint32_t tier1_frame_execution_argument(uint32_t index)
{
    return 0x123400u + index * 257;
}

static uint32_t tier1_frame_execution_sentinel(uint32_t reg)
{
    return 0x413200u + reg * 263;
}

static void tier1_frame_execution_check_memory(Tier1EmitBuffer* buffer, uint32_t base, int32_t offset, uint32_t expected)
{
    tier1_frame_emit_add(buffer, tier1_frame_emit_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, base, offset));
    tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_XOR_RM64_IMM32, X86_REGISTER_RAX, (int32_t)expected));
    tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_OR_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RCX));
}

static uint8_t* tier1_frame_execution_load(Tier1FrameExecutionTest* test, Tier1EmitBuffer* buffer, uint32_t area)
{
    uint32_t offsets[BUSTER_TIER1_FRAME_EXEC_RECORDS];
    uint8_t* mapping = test->code + area * BUSTER_TIER1_FRAME_EXEC_CODE_BYTES;
    // The small mapped prefix also makes sanitizers' optional function-type probes safe for a generated entry.
    uint8_t* entry = mapping + 64;
    bool valid = buffer->count <= BUSTER_TIER1_FRAME_EXEC_RECORDS;
    const BranchSizingCheck check = branch_sizing_validate(buffer->records, buffer->count, buffer->branches, buffer->targets, buffer->branch_count);
    valid &= check.error == BRANCH_SIZING_OK;
    tier1_frame_execution_expect(test, valid, "valid record and absolute branch columns");
    if (valid)
    {
        const BranchSizing sizing = size_branches(buffer->records, buffer->count, buffer->branches, buffer->targets, buffer->branch_count, offsets);
        valid = sizing.error == BRANCH_SIZING_OK;
        valid &= x86_target_validate(test->allowed, buffer->records, buffer->count) == buffer->count;
        tier1_frame_execution_expect(test, valid, "branch sizing and baseline target validation");
    }
    if (valid)
    {
        valid = mprotect(mapping, BUSTER_TIER1_FRAME_EXEC_CODE_BYTES, PROT_READ | PROT_WRITE) == 0;
        if (valid)
        {
            memset(mapping, 0, 64);
            valid = x86_encode(buffer->records, buffer->count, entry, BUSTER_TIER1_FRAME_EXEC_CODE_BYTES - 64, 0, 0) != 0;
            valid &= mprotect(mapping, BUSTER_TIER1_FRAME_EXEC_CODE_BYTES, PROT_READ | PROT_EXEC) == 0;
        }
        tier1_frame_execution_expect(test, valid, "encode and protect generated frame function");
    }
    return valid ? entry : 0;
}

static uint8_t* tier1_frame_execution_caller(Tier1FrameExecutionTest* test, const uint8_t* target, uintptr_t top, uint32_t al)
{
    X86Instruction records[BUSTER_TIER1_FRAME_EXEC_RECORDS];
    uint32_t branches[16];
    uint32_t targets[16];
    Tier1EmitBuffer buffer = {
        .records = records,
        .capacity = BUSTER_TIER1_FRAME_EXEC_RECORDS,
        .branches = branches,
        .targets = targets,
        .branch_capacity = 16,
    };
    const uint32_t saved[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
    for (uint32_t i = 0; i < 6; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_PUSH_R64, 0, saved[i]));
    }
    // RBX's sentinel is the caller's stack pointer. A broken restore fails on the stack switch or final pops too.
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RSP));
    for (uint32_t i = 1; i < 6; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(saved[i], tier1_frame_execution_sentinel(saved[i])));
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RSP, top));
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_R10, (uintptr_t)test->vectors));
    for (uint32_t i = 0; i < 8; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOVUPS_X_XM, i, X86_REGISTER_R10, (int32_t)(i * 16)));
    }
    for (uint32_t i = 0; i < BUSTER_ABI_INTEGER_ARGUMENTS; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(abi_integer_arguments[i], tier1_frame_execution_argument(i)));
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_R11, (uintptr_t)target));
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, al));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_CALL_RM64, 0, X86_REGISTER_R11));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, X86_REGISTER_RBX));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_RAX, 0));
    tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    for (uint32_t i = 1; i < 6; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, saved[i], (int32_t)tier1_frame_execution_sentinel(saved[i])));
        tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0));
    const uint32_t done = tier1_frame_emit_branch(&buffer, X86_FORM_JMP_REL8, 0);
    for (uint32_t i = 0; i < done; i += 1)
    {
        buffer.targets[i] = buffer.count;
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 1));
    buffer.targets[done] = buffer.count;
    for (uint32_t i = 6; i != 0; i -= 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_POP_R64, 0, saved[i - 1]));
    }
    tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
    return tier1_frame_execution_load(test, &buffer, 1);
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) uint64_t tier1_frame_execution_invoke(const uint8_t* entry)
{
    uint64_t (*function)(void);
    memcpy(&function, &entry, sizeof(function));
    return function();
}

static uint8_t* tier1_frame_execution_function(Tier1FrameExecutionTest* test, const Tier1Frame* frame, uint32_t flags, uint32_t size, uint32_t alignment, uint32_t used, uint32_t al, bool check_body)
{
    X86Instruction records[BUSTER_TIER1_FRAME_EXEC_RECORDS];
    uint32_t branches[128];
    uint32_t targets[128];
    Tier1EmitBuffer buffer = {
        .records = records,
        .capacity = BUSTER_TIER1_FRAME_EXEC_RECORDS,
        .branches = branches,
        .targets = targets,
        .branch_capacity = 128,
    };
    // A preceding record makes absolute relocation errors observable in every probe/variadic branch.
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R10));
    bool valid = tier1_frame_emit(frame, false, &buffer) == TIER1_FRAME_EMIT_OK;
    tier1_frame_execution_expect(test, valid, "append native prologue");
    if (valid && check_body)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RAX));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RCX, 255));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_XOR_RM64_IMM32, X86_REGISTER_RCX, (int32_t)al));
        tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0x12345678));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, frame->base_register, frame->offset[0]));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, frame->base_register, frame->offset[0] + (int32_t)size - 8));
        if ((flags & BUSTER_TIER1_FRAME_DYNAMIC_STACK) != 0)
        {
            tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, 512));
        }
        tier1_frame_execution_check_memory(&buffer, frame->base_register, frame->offset[0], 0x12345678);
        tier1_frame_execution_check_memory(&buffer, frame->base_register, frame->offset[0] + (int32_t)size - 8, 0x12345678);
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RAX, frame->base_register, frame->offset[0]));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RAX, (int32_t)alignment - 1));
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_OR_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RCX));
        if (frame->variadic)
        {
            for (uint32_t i = 0; i < BUSTER_ABI_INTEGER_ARGUMENTS; i += 1)
            {
                tier1_frame_execution_check_memory(&buffer, frame->base_register, frame->variadic_offset + (int32_t)(i * 8), tier1_frame_execution_argument(i));
            }
            for (uint32_t i = 0; i < 16; i += 1)
            {
                const uint32_t expected = al == 0 ? 0 : (uint32_t)test->vectors[i];
                tier1_frame_execution_check_memory(&buffer, frame->base_register, frame->variadic_offset + BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + (int32_t)(i * 8), expected);
            }
        }
        for (uint32_t reg = 0; reg < 16; reg += 1)
        {
            if ((used & (1u << reg)) != 0)
            {
                tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(reg, 0xdead));
            }
        }
        tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX));
    }
    else if (valid)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0));
    }
    if (valid)
    {
        valid = tier1_frame_emit(frame, true, &buffer) == TIER1_FRAME_EMIT_OK;
        tier1_frame_execution_expect(test, valid, "append native epilogue");
        tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
    }
    return valid ? tier1_frame_execution_load(test, &buffer, 0) : 0;
}

static void tier1_frame_execution_matrix(Tier1FrameExecutionTest* test)
{
    const uint32_t alignments[] = { 8, 16, 64, 8192 };
    const uint32_t sizes[] = { 8, 8200 };
    const uint32_t start = 0;
    const uint32_t end = 1;
    const uint32_t all_saved = (1u << X86_REGISTER_RBX) | (1u << X86_REGISTER_RBP) | (15u << X86_REGISTER_R12);
    for (uint32_t flags = 0; flags <= BUSTER_TIER1_FRAME_FLAGS; flags += 1)
    {
        for (uint32_t a = 0; a < 4; a += 1)
        {
            for (uint32_t s = 0; s < 2; s += 1)
            {
                for (uint32_t save = 0; save < 2; save += 1)
                {
                    Tier1FrameRequest request = {
                        .slots = {
                            .start = &start,
                            .end = &end,
                            .size = sizes + s,
                            .alignment = alignments + a,
                            .count = 1,
                            .column_count = 1,
                        },
                        .flags = flags,
                    };
                    Tier1FrameRequirements requirements;
                    Tier1Frame frame;
                    const Tier1FrameError required = tier1_frame_requirements(&request, &requirements);
                    tier1_frame_execution_expect(test, required == TIER1_FRAME_OK, "preallocation requirements");
                    request.used_callee_saved = save != 0 ? all_saved & ~requirements.reserved_registers : 0;
                    const Tier1FrameError planned = tier1_frame_plan(&request, &test->output, &test->scratch, &frame);
                    tier1_frame_execution_expect(test, planned == TIER1_FRAME_OK, "native frame planned");
                    if (planned == TIER1_FRAME_OK)
                    {
                        const uint32_t al_count = frame.variadic ? 3 : 1;
                        for (uint32_t count = 0; count < al_count; count += 1)
                        {
                            const uint32_t al = count == 2 ? 8 : count;
                            uint8_t* function = tier1_frame_execution_function(test, &frame, flags, sizes[s], alignments[a], request.used_callee_saved, al, true);
                            for (uint32_t residue = 0; residue < 2 && function != 0; residue += 1)
                            {
                                const uintptr_t top = (((uintptr_t)test->stack + 384 * 1024) & ~(uintptr_t)65535) - residue * 48;
                                memset((void*)(top - 128 * 1024), 0, 128 * 1024);
                                uint8_t* caller = tier1_frame_execution_caller(test, function, top, al);
                                if (caller != 0)
                                {
                                    const uint64_t result = tier1_frame_execution_invoke(caller);
                                    if (result != 0 && test->failures < 20)
                                    {
                                        fprintf(stderr, "frame case flags=%u align=%u size=%u saves=%u AL=%u residue=%u\n", flags, alignments[a], sizes[s], save, al, residue);
                                    }
                                    tier1_frame_execution_expect(test, result == 0, "native objects, AL, full variadic saves, dynamic stack, and all callee saves");
                                }
                            }
                        }
                    }
                    arena_pop_to(&test->output, 0);
                }
            }
        }
    }
}

static void tier1_frame_execution_outgoing(Tier1FrameExecutionTest* test)
{
    const uint32_t start = 0;
    const uint32_t end = 1;
    const uint32_t size = 32;
    const uint32_t alignment = 16;
    for (uint32_t dynamic = 0; dynamic < 2; dynamic += 1)
    {
        for (uint32_t overalign = 0; overalign < 2; overalign += 1)
        {
            const AbiCall call = {
                .stack_bytes = 80,
                .stack_alignment_log2 = overalign != 0 ? 6 : 4,
            };
            const uint32_t call_alignment = 1u << call.stack_alignment_log2;
            Tier1FrameRequest request = {
                .slots = {
                    .start = &start,
                    .end = &end,
                    .size = &size,
                    .alignment = &alignment,
                    .count = 1,
                    .column_count = 1,
                },
                .calls = &call,
                .call_count = 1,
                .flags = dynamic != 0 ? BUSTER_TIER1_FRAME_DYNAMIC_STACK : 0,
            };
            Tier1Frame frame;
            const Tier1FrameError planned = tier1_frame_plan(&request, &test->output, &test->scratch, &frame);
            tier1_frame_execution_expect(test, planned == TIER1_FRAME_OK, "outgoing call frame planned");
            if (planned == TIER1_FRAME_OK)
            {
                X86Instruction records[BUSTER_TIER1_FRAME_EXEC_RECORDS];
                uint32_t branches[128];
                uint32_t targets[128];
                Tier1EmitBuffer buffer = {
                    .records = records,
                    .capacity = BUSTER_TIER1_FRAME_EXEC_RECORDS,
                    .branches = branches,
                    .targets = targets,
                    .branch_capacity = 128,
                };
                tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RCX, X86_REGISTER_RSP, 8));
                tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RCX, (int32_t)call_alignment - 1));
                tier1_frame_execution_check_memory(&buffer, X86_REGISTER_RSP, 8, 0x3456);
                tier1_frame_execution_check_memory(&buffer, X86_REGISTER_RSP, (int32_t)call.stack_bytes, 0x789a);
                tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0));
                tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, 8));
                tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, (int32_t)call.stack_bytes));
                tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX));
                tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
                uint8_t* callee = tier1_frame_execution_load(test, &buffer, 2);
                buffer.count = 0;
                buffer.branch_count = 0;
                bool valid = callee != 0 && tier1_frame_emit(&frame, false, &buffer) == TIER1_FRAME_EMIT_OK;
                if (valid)
                {
                    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0x12345678));
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, frame.base_register, frame.offset[0]));
                    if (dynamic != 0)
                    {
                        // The call consumer owns each dynamic call area. The fixed frame stays addressable after
                        // an arbitrary dynamic subtraction and the stronger call alignment.
                        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, 528));
                        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RSP, -(int32_t)call_alignment));
                        const uint32_t bytes = (frame.outgoing_bytes + call_alignment - 1) & ~(call_alignment - 1);
                        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, (int32_t)bytes));
                    }
                    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0x3456));
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, 0));
                    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0x789a));
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, (int32_t)call.stack_bytes - 8));
                    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_R11, (uintptr_t)callee));
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_CALL_RM64, 0, X86_REGISTER_R11));
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RAX));
                    tier1_frame_execution_check_memory(&buffer, frame.base_register, frame.offset[0], 0x12345678);
                    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX));
                    valid = tier1_frame_emit(&frame, true, &buffer) == TIER1_FRAME_EMIT_OK;
                    tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
                }
                tier1_frame_execution_expect(test, valid, "emit actual outgoing call and stable local access");
                uint8_t* function = valid ? tier1_frame_execution_load(test, &buffer, 0) : 0;
                const uintptr_t top = ((uintptr_t)test->stack + 384 * 1024) & ~(uintptr_t)65535;
                uint8_t* caller = function != 0 ? tier1_frame_execution_caller(test, function, top, 0) : 0;
                if (caller != 0)
                {
                    tier1_frame_execution_expect(test, tier1_frame_execution_invoke(caller) == 0, "native fixed/dynamic call stack arguments, over-alignment, and intact locals");
                }
            }
            arena_pop_to(&test->output, 0);
        }
    }
}

static void tier1_frame_execution_probe(Tier1FrameExecutionTest* test, uint32_t shape)
{
    const uintptr_t boundary = ((uintptr_t)test->stack + 256 * 1024) & ~(uintptr_t)65535;
    const uintptr_t top = shape < 2 ? boundary + 32768 : boundary + 16;
    const uintptr_t guard = shape == 0 ? top - 3 * 4096 : (shape == 1 ? boundary + 16384 : boundary - 2 * 4096);
    const uint32_t size = shape == 0 ? 32768 : (shape == 1 ? 8 : 4112);
    const uint32_t alignment = shape == 1 ? 65536 : 16;
    const uint32_t start = 0;
    const uint32_t end = 1;
    for (uint32_t protect = 0; protect < 2; protect += 1)
    {
        for (uint32_t probe = 0; probe < 2; probe += 1)
        {
            Tier1FrameRequest request = {
                .slots = {
                    .start = &start,
                    .end = &end,
                    .size = &size,
                    .alignment = &alignment,
                    .count = 1,
                    .column_count = 1,
                },
                .flags = BUSTER_TIER1_FRAME_NO_RED_ZONE | (probe != 0 ? BUSTER_TIER1_FRAME_PROBE_STACK : 0),
            };
            Tier1Frame frame;
            const Tier1FrameError planned = tier1_frame_plan(&request, &test->output, &test->scratch, &frame);
            tier1_frame_execution_expect(test, planned == TIER1_FRAME_OK, "probe frame planned");
            if (planned == TIER1_FRAME_OK)
            {
                uint8_t* function = tier1_frame_execution_function(test, &frame, request.flags, size, alignment, 0, 0, false);
                uint8_t* caller = function != 0 ? tier1_frame_execution_caller(test, function, top, 0) : 0;
                const bool protection = mprotect((void*)guard, 4096, protect != 0 ? PROT_NONE : PROT_READ | PROT_WRITE) == 0;
                tier1_frame_execution_expect(test, protection, "install intermediate/final guard page");
                if (caller != 0 && protection)
                {
                    fflush(0);
                    const pid_t child = fork();
                    if (child == 0)
                    {
                        // A deliberate guard access is checked by waitpid, including under ASan/UBSan.
                        signal(SIGSEGV, SIG_DFL);
                        signal(SIGBUS, SIG_DFL);
                        alarm(5);
                        const struct rlimit no_core = {0};
                        setrlimit(RLIMIT_CORE, &no_core);
                        _exit(tier1_frame_execution_invoke(caller) != 0);
                    }
                    int status = 0;
                    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
                    const bool fault = waited && WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
                    const bool passed = protect != 0 && probe != 0 ? fault : waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
                    tier1_frame_execution_expect(test, passed, "only actual probing faults on skipped or final guard page");
                }
                tier1_frame_execution_expect(test, mprotect((void*)guard, 4096, PROT_READ | PROT_WRITE) == 0, "restore probe guard page");
            }
            arena_pop_to(&test->output, 0);
        }
    }
}

static void tier1_frame_execution_errors(Tier1FrameExecutionTest* test)
{
    X86Instruction records[BUSTER_TIER1_FRAME_EMIT_RECORDS + 2];
    X86Instruction original[BUSTER_TIER1_FRAME_EMIT_RECORDS + 2];
    uint32_t branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 2];
    uint32_t targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 2];
    uint32_t original_branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 2];
    uint32_t original_targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 2];
    memset(records, 0xa5, sizeof(records));
    memset(branches, 0xb6, sizeof(branches));
    memset(targets, 0xc7, sizeof(targets));
    memcpy(original, records, sizeof(records));
    memcpy(original_branches, branches, sizeof(branches));
    memcpy(original_targets, targets, sizeof(targets));
    Tier1Frame frame = { .prologue_count = 1 };
    frame.prologue[0] = (Tier1FrameStep){
        .kind = TIER1_FRAME_PROBE_ALIGN,
        .reg = X86_REGISTER_RSP,
        .base = X86_REGISTER_RSP,
        .amount = 65536,
    };
    for (uint32_t which = 0; which < 7; which += 1)
    {
        Tier1EmitBuffer buffer = {
            .records = records,
            .count = 1,
            .capacity = BUSTER_TIER1_FRAME_EMIT_RECORDS + 1,
            .branches = branches,
            .targets = targets,
            .branch_count = 1,
            .branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1,
        };
        Tier1Frame broken = frame;
        Tier1FrameEmitError expected = TIER1_FRAME_EMIT_INVALID;
        switch (which)
        {
        case 0:
            buffer.capacity = buffer.count + 11;
            expected = TIER1_FRAME_EMIT_CAPACITY;
            break;
        case 1:
            buffer.branch_capacity = buffer.branch_count + 1;
            expected = TIER1_FRAME_EMIT_CAPACITY;
            break;
        case 2:
            broken.prologue[0].amount = 65535;
            break;
        case 3:
            broken.prologue_count = BUSTER_TIER1_FRAME_STEP_CAPACITY + 1;
            break;
        case 4:
            broken.prologue[0].kind = (Tier1FrameStepKind)99;
            break;
        case 5:
            buffer.records = 0;
            break;
        case 6:
            buffer.count = buffer.capacity + 1;
            break;
        }
        const Tier1EmitBuffer before = buffer;
        const Tier1FrameEmitError error = tier1_frame_emit(&broken, false, &buffer);
        tier1_frame_execution_expect(test, error == expected, "explicit malformed/capacity refusal");
        tier1_frame_execution_expect(test, memcmp(&buffer, &before, sizeof(buffer)) == 0, "failure preserves append metadata");
        tier1_frame_execution_expect(test, memcmp(records, original, sizeof(records)) == 0 && memcmp(branches, original_branches, sizeof(branches)) == 0 && memcmp(targets, original_targets, sizeof(targets)) == 0, "failure preserves all destination bytes");
    }
    Tier1Frame empty = {0};
    Tier1EmitBuffer empty_buffer = {0};
    tier1_frame_execution_expect(test, tier1_frame_emit(&empty, false, &empty_buffer) == TIER1_FRAME_EMIT_OK && tier1_frame_emit(&empty, true, &empty_buffer) == TIER1_FRAME_EMIT_OK, "empty frame permits null storage");
    Tier1Frame maximum = { .prologue_count = BUSTER_TIER1_FRAME_STEP_CAPACITY };
    for (uint32_t i = 0; i < BUSTER_TIER1_FRAME_STEP_CAPACITY; i += 1)
    {
        maximum.prologue[i] = frame.prologue[0];
    }
    Tier1EmitBuffer maximum_buffer = {
        .records = records,
        .count = 1,
        .capacity = BUSTER_TIER1_FRAME_EMIT_RECORDS + 1,
        .branches = branches,
        .targets = targets,
        .branch_count = 1,
        .branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1,
    };
    tier1_frame_execution_expect(test, tier1_frame_emit(&maximum, false, &maximum_buffer) == TIER1_FRAME_EMIT_OK, "maximum twelve-record/two-branch expansion per step");
    tier1_frame_execution_expect(test, maximum_buffer.count == BUSTER_TIER1_FRAME_EMIT_RECORDS + 1 && maximum_buffer.branch_count == BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1, "published conservative append bounds are sufficient");
    tier1_frame_execution_expect(test, memcmp(records, original, sizeof(X86Instruction)) == 0 && memcmp(records + BUSTER_TIER1_FRAME_EMIT_RECORDS + 1, original + BUSTER_TIER1_FRAME_EMIT_RECORDS + 1, sizeof(X86Instruction)) == 0 && branches[0] == original_branches[0] && targets[0] == original_targets[0] && branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1] == original_branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1] && targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1] == original_targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES + 1], "maximum append preserves prefix and suffix canaries");
    for (uint32_t i = 1; i < maximum_buffer.branch_count; i += 1)
    {
        tier1_frame_execution_expect(test, maximum_buffer.branches[i] < maximum_buffer.count && maximum_buffer.targets[i] >= 1 && maximum_buffer.targets[i] <= maximum_buffer.count && (i == 1 || maximum_buffer.branches[i] > maximum_buffer.branches[i - 1]), "maximum append retains ordered absolute branch indices");
    }
    const long page_bytes = sysconf(_SC_PAGESIZE);
    if (page_bytes == 4096)
    {
        uint8_t* mapping = mmap(0, 6 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        bool protected = mapping != MAP_FAILED;
        for (uint32_t i = 1; i < 6 && protected; i += 2)
        {
            protected = mprotect(mapping + i * 4096, 4096, PROT_NONE) == 0;
        }
        tier1_frame_execution_expect(test, protected, "exact-capacity output guard mapping");
        if (protected)
        {
            Tier1EmitBuffer buffer = {
                .records = (X86Instruction*)(mapping + 4096 - 12 * sizeof(X86Instruction)),
                .capacity = 12,
                .branches = (uint32_t*)(mapping + 3 * 4096 - 2 * sizeof(uint32_t)),
                .targets = (uint32_t*)(mapping + 5 * 4096 - 2 * sizeof(uint32_t)),
                .branch_capacity = 2,
            };
            tier1_frame_execution_expect(test, tier1_frame_emit(&frame, false, &buffer) == TIER1_FRAME_EMIT_OK && buffer.count == 12 && buffer.branch_count == 2, "exact output capacity at guard pages");
        }
        if (mapping != MAP_FAILED)
        {
            munmap(mapping, 6 * 4096);
        }
    }
}

static int run_tier1_frame_execution_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-tier1-frame-execution\n");
    }
    else if (!x86_encoder_initialize())
    {
        fprintf(stderr, "test-tier1-frame-execution: encoder initialization failed\n");
    }
    else
    {
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            Tier1FrameExecutionTest test = {0};
            test.code = mmap(0, 3 * BUSTER_TIER1_FRAME_EXEC_CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            test.stack = mmap(0, BUSTER_TIER1_FRAME_EXEC_STACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            test.allowed = malloc(X86_FORM_COUNT);
            test.output = arena_reserve(64 * 1024);
            test.scratch = arena_reserve(64 * 1024);
            if (test.code == MAP_FAILED || test.stack == MAP_FAILED || test.allowed == 0 || test.output.base == 0 || test.scratch.base == 0)
            {
                _exit(1);
            }
            const X86Target target = { .architecture = X86_ARCH_BASELINE };
            x86_target_forms(&target, test.allowed);
            for (uint32_t i = 0; i < 16; i += 1)
            {
                test.vectors[i] = 0x654300u + i * 267;
            }
            tier1_frame_execution_errors(&test);
            tier1_frame_execution_matrix(&test);
            tier1_frame_execution_outgoing(&test);
            for (uint32_t shape = 0; shape < 3; shape += 1)
            {
                tier1_frame_execution_probe(&test, shape);
            }
            printf("tier1 frame execution: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
            arena_release(&test.scratch);
            arena_release(&test.output);
            munmap(test.stack, BUSTER_TIER1_FRAME_EXEC_STACK_BYTES);
            munmap(test.code, 3 * BUSTER_TIER1_FRAME_EXEC_CODE_BYTES);
            free(test.allowed);
            fflush(0);
            _exit(test.failures != 0);
        }
        int status = 0;
        if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
        else
        {
            fprintf(stderr, "test-tier1-frame-execution: native child failed (status %d)\n", status);
        }
    }
    return result;
}
