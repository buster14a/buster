// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Native x86-64 SysV consumer of tier1_frame.c's bounded prologue/epilogue steps. Include after the frame planner and
// x86_64_encoder.c. The shared append buffer also belongs to the Tier 1 instruction consumer: branches and targets
// are absolute record indices, ready for size_branches once the whole function is present. No byte offsets or unwind
// rows are published here; #88 still needs actual instruction-PC events from the final encoded function.
//
// R11 is the only temporary register. Reserve it before allocation, in addition to tier1_frame_requirements' mask.
// Probes preserve the incoming argument registers, XMM registers, and AL. The conditional vector save tests entry AL
// before any consumer may change it. Epilogues preserve result registers. Flags are not preserved. Dynamic outgoing
// call areas remain the instruction consumer's responsibility; fixed locals use frame.base_register + frame.offset.
//
// The bounded plan expands into at most twelve records and two branches per step. A local transaction is validated
// and copied only if the complete append fits. On any error both buffer counts and all caller storage are unchanged.
// Existing record/branch prefixes, frame, and destination arrays must be disjoint from the new appended ranges.
// The fixed, at-most-32-step control protocol is scalar; input-sized frame coloring retains its data-parallel twin.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#define BUSTER_TIER1_FRAME_EMIT_SCRATCH X86_REGISTER_R11
#define BUSTER_TIER1_FRAME_EMIT_RECORDS (BUSTER_TIER1_FRAME_STEP_CAPACITY * 12u)
#define BUSTER_TIER1_FRAME_EMIT_BRANCHES (BUSTER_TIER1_FRAME_STEP_CAPACITY * 2u)

typedef struct Tier1EmitBuffer Tier1EmitBuffer;
struct Tier1EmitBuffer
{
    X86Instruction* records;
    uint32_t count;
    uint32_t capacity;
    uint32_t* branches;
    uint32_t* targets;
    uint32_t branch_count;
    uint32_t branch_capacity;
};

typedef enum Tier1FrameEmitError
{
    TIER1_FRAME_EMIT_OK,
    TIER1_FRAME_EMIT_INVALID,
    TIER1_FRAME_EMIT_CAPACITY,
} Tier1FrameEmitError;

static __attribute__((unused)) Tier1FrameEmitError tier1_frame_emit(const Tier1Frame* frame, bool epilogue, Tier1EmitBuffer* buffer);

static X86Instruction tier1_frame_emit_register(uint16_t form, uint32_t reg, uint32_t rm)
{
    const X86Instruction record = {
        .form = form,
        .reg = (uint8_t)reg,
        .rm = (uint8_t)rm,
    };
    return record;
}

static X86Instruction tier1_frame_emit_immediate(uint16_t form, uint32_t rm, int32_t immediate)
{
    const X86Instruction record = {
        .form = form,
        .rm = (uint8_t)rm,
        .immediate = immediate,
    };
    return record;
}

static X86Instruction tier1_frame_emit_memory(uint16_t form, uint32_t reg, uint32_t base, int32_t offset)
{
    const X86Instruction record = {
        .form = form,
        .reg = (uint8_t)reg,
        .rm = (uint8_t)base,
        .mode = BUSTER_X86_MEMORY_BASE,
        .displacement = offset,
    };
    return record;
}

static void tier1_frame_emit_add(Tier1EmitBuffer* buffer, X86Instruction record)
{
    buffer->records[buffer->count] = record;
    buffer->count += 1;
}

static uint32_t tier1_frame_emit_branch(Tier1EmitBuffer* buffer, uint16_t form, uint32_t target)
{
    const uint32_t index = buffer->branch_count;
    buffer->branches[index] = buffer->count;
    buffer->targets[index] = target;
    buffer->branch_count += 1;
    tier1_frame_emit_add(buffer, (X86Instruction){ .form = form });
    return index;
}

static void tier1_frame_emit_touch(Tier1EmitBuffer* buffer)
{
    // A byte read/write never crosses a page boundary and leaves existing stack data unchanged.
    tier1_frame_emit_add(buffer, tier1_frame_emit_memory(X86_FORM_OR_RM8_IMM8, 0, X86_REGISTER_RSP, 0));
}

static void tier1_frame_emit_probe_allocate(uint32_t amount, Tier1EmitBuffer* buffer)
{
    const uint32_t pages = amount & ~(BUSTER_TIER1_FRAME_PAGE_BYTES - 1);
    const uint32_t remainder = amount - pages;
    if (pages != 0)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, BUSTER_TIER1_FRAME_EMIT_SCRATCH, (int32_t)pages));
        const uint32_t loop = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, BUSTER_TIER1_FRAME_PAGE_BYTES));
        tier1_frame_emit_touch(buffer);
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        tier1_frame_emit_branch(buffer, X86_FORM_JNE_REL8, loop);
    }
    if (remainder != 0)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, (int32_t)remainder));
        tier1_frame_emit_touch(buffer);
    }
}

static void tier1_frame_emit_probe_align(uint32_t alignment, Tier1EmitBuffer* buffer)
{
    if (alignment <= BUSTER_TIER1_FRAME_PAGE_BYTES)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RSP, -(int32_t)alignment));
        tier1_frame_emit_touch(buffer);
    }
    else
    {
        // R11 holds target + one page. Never subtract a page when it would pass the target; touch the final partial
        // step as well. This also covers an already-aligned RSP and preserves every incoming argument register.
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, BUSTER_TIER1_FRAME_EMIT_SCRATCH, -(int32_t)alignment));
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_ADD_RM64_IMM32, BUSTER_TIER1_FRAME_EMIT_SCRATCH, BUSTER_TIER1_FRAME_PAGE_BYTES));
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        const uint32_t finish = tier1_frame_emit_branch(buffer, X86_FORM_JB_REL8, 0);
        const uint32_t loop = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, BUSTER_TIER1_FRAME_PAGE_BYTES));
        tier1_frame_emit_touch(buffer);
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        tier1_frame_emit_branch(buffer, X86_FORM_JAE_REL8, loop);
        buffer->targets[finish] = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, BUSTER_TIER1_FRAME_EMIT_SCRATCH, BUSTER_TIER1_FRAME_PAGE_BYTES));
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, BUSTER_TIER1_FRAME_EMIT_SCRATCH));
        tier1_frame_emit_touch(buffer);
    }
}

static bool tier1_frame_emit_step_valid(const Tier1FrameStep* step, bool epilogue)
{
    bool valid = step->base < 16;
    switch (step->kind)
    {
    case TIER1_FRAME_PUSH_POINTER:
    case TIER1_FRAME_POP_POINTER:
        valid &= step->reg == X86_REGISTER_RBP && step->base == X86_REGISTER_RSP && step->amount == 8;
        valid &= epilogue == (step->kind == TIER1_FRAME_POP_POINTER);
        break;
    case TIER1_FRAME_SET_POINTER:
        valid &= !epilogue && step->reg == X86_REGISTER_RBP && step->base == X86_REGISTER_RSP;
        break;
    case TIER1_FRAME_SET_REALIGN_BASE:
        valid &= !epilogue && step->reg == BUSTER_TIER1_FRAME_REALIGN_BASE && step->base == X86_REGISTER_RSP;
        break;
    case TIER1_FRAME_RESET_STACK:
        valid &= epilogue && step->reg == X86_REGISTER_RSP && step->base == X86_REGISTER_RBP;
        break;
    case TIER1_FRAME_ALIGN_STACK:
    case TIER1_FRAME_PROBE_ALIGN:
        valid &= !epilogue && step->reg == X86_REGISTER_RSP && step->base == X86_REGISTER_RSP;
        valid &= step->amount >= 16 && step->amount <= BUSTER_TIER1_FRAME_MAX_ALIGNMENT && (step->amount & (step->amount - 1)) == 0;
        break;
    case TIER1_FRAME_ALLOCATE:
    case TIER1_FRAME_PROBE_ALLOCATE:
    case TIER1_FRAME_RELEASE:
        valid &= step->reg == X86_REGISTER_RSP && step->base == X86_REGISTER_RSP && step->amount != 0 && step->amount <= INT32_MAX;
        valid &= epilogue == (step->kind == TIER1_FRAME_RELEASE);
        break;
    case TIER1_FRAME_SAVE_REGISTER:
    case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
    case TIER1_FRAME_RESTORE_REGISTER:
        valid &= step->reg < 16 && step->reg != X86_REGISTER_RSP && step->amount == 8;
        valid &= epilogue == (step->kind == TIER1_FRAME_RESTORE_REGISTER);
        break;
    case TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL:
        valid &= !epilogue && step->reg >= BUSTER_ABI_REGISTER_VECTOR && step->reg < BUSTER_ABI_REGISTER_VECTOR + BUSTER_ABI_VECTOR_ARGUMENTS;
        valid &= step->amount == BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES;
        break;
    default:
        valid = false;
        break;
    }
    return valid;
}

static Tier1FrameEmitError tier1_frame_emit(const Tier1Frame* frame, bool epilogue, Tier1EmitBuffer* buffer)
{
    Tier1FrameEmitError error = frame != 0 && buffer != 0 ? TIER1_FRAME_EMIT_OK : TIER1_FRAME_EMIT_INVALID;
    X86Instruction records[BUSTER_TIER1_FRAME_EMIT_RECORDS];
    uint32_t branches[BUSTER_TIER1_FRAME_EMIT_BRANCHES];
    uint32_t targets[BUSTER_TIER1_FRAME_EMIT_BRANCHES];
    Tier1EmitBuffer temporary = {
        .records = records,
        .branches = branches,
        .targets = targets,
    };
    uint32_t count = 0;
    const Tier1FrameStep* steps = 0;
    if (error == TIER1_FRAME_EMIT_OK)
    {
        count = epilogue ? frame->epilogue_count : frame->prologue_count;
        steps = epilogue ? frame->epilogue : frame->prologue;
        if (count > BUSTER_TIER1_FRAME_STEP_CAPACITY || buffer->count > buffer->capacity || buffer->branch_count > buffer->branch_capacity)
        {
            error = TIER1_FRAME_EMIT_INVALID;
        }
    }
    for (uint32_t i = 0; i < count && error == TIER1_FRAME_EMIT_OK; i += 1)
    {
        const Tier1FrameStep step = steps[i];
        if (!tier1_frame_emit_step_valid(&step, epilogue))
        {
            error = TIER1_FRAME_EMIT_INVALID;
        }
        else
        {
            switch (step.kind)
            {
            case TIER1_FRAME_PUSH_POINTER:
            case TIER1_FRAME_POP_POINTER:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_register(step.kind == TIER1_FRAME_PUSH_POINTER ? X86_FORM_PUSH_R64 : X86_FORM_POP_R64, 0, step.reg));
                break;
            case TIER1_FRAME_SET_POINTER:
            case TIER1_FRAME_SET_REALIGN_BASE:
            case TIER1_FRAME_RESET_STACK:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, step.reg, step.base));
                break;
            case TIER1_FRAME_ALIGN_STACK:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RSP, -(int32_t)step.amount));
                break;
            case TIER1_FRAME_PROBE_ALIGN:
                tier1_frame_emit_probe_align(step.amount, &temporary);
                break;
            case TIER1_FRAME_ALLOCATE:
            case TIER1_FRAME_RELEASE:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(step.kind == TIER1_FRAME_ALLOCATE ? X86_FORM_SUB_RM64_IMM32 : X86_FORM_ADD_RM64_IMM32, X86_REGISTER_RSP, (int32_t)step.amount));
                break;
            case TIER1_FRAME_PROBE_ALLOCATE:
                tier1_frame_emit_probe_allocate(step.amount, &temporary);
                break;
            case TIER1_FRAME_SAVE_REGISTER:
            case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
            case TIER1_FRAME_RESTORE_REGISTER:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_memory(step.kind == TIER1_FRAME_RESTORE_REGISTER ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_RM64_R64, step.reg, step.base, step.offset));
                break;
            case TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL:
            {
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(X86_FORM_TEST_RM8_IMM8, X86_REGISTER_RAX, 255));
                const uint32_t skip = tier1_frame_emit_branch(&temporary, X86_FORM_JE_REL8, 0);
                do
                {
                    const Tier1FrameStep vector = steps[i];
                    if (!tier1_frame_emit_step_valid(&vector, epilogue))
                    {
                        error = TIER1_FRAME_EMIT_INVALID;
                    }
                    else
                    {
                        tier1_frame_emit_add(&temporary, tier1_frame_emit_memory(X86_FORM_MOVAPS_XM_X, vector.reg - BUSTER_ABI_REGISTER_VECTOR, vector.base, vector.offset));
                    }
                    i += 1;
                } while (i < count && steps[i].kind == TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL && error == TIER1_FRAME_EMIT_OK);
                i -= 1;
                temporary.targets[skip] = temporary.count;
                break;
            }
            }
        }
    }
    if (error == TIER1_FRAME_EMIT_OK)
    {
        if (temporary.count > buffer->capacity - buffer->count || temporary.branch_count > buffer->branch_capacity - buffer->branch_count)
        {
            error = TIER1_FRAME_EMIT_CAPACITY;
        }
        else if ((temporary.count != 0 && buffer->records == 0) || (temporary.branch_count != 0 && (buffer->branches == 0 || buffer->targets == 0)))
        {
            error = TIER1_FRAME_EMIT_INVALID;
        }
    }
    if (error == TIER1_FRAME_EMIT_OK)
    {
        for (uint32_t i = 0; i < temporary.branch_count; i += 1)
        {
            buffer->branches[buffer->branch_count + i] = buffer->count + temporary.branches[i];
            buffer->targets[buffer->branch_count + i] = buffer->count + temporary.targets[i];
        }
        if (temporary.count != 0)
        {
            memcpy(buffer->records + buffer->count, temporary.records, (uint64_t)temporary.count * sizeof(X86Instruction));
        }
        buffer->count += temporary.count;
        buffer->branch_count += temporary.branch_count;
    }
    return error;
}
