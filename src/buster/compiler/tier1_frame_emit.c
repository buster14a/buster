// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Native x86-64 SysV consumer of tier1_frame.c's bounded prologue/epilogue steps. Include after the frame planner and
// x86_64_encoder.c. The shared append buffer also belongs to the Tier 1 instruction consumer: branches and targets
// are absolute record indices, ready for size_branches once the whole function is present. No byte offsets or unwind
// rows are published here. Without probes the records are Tier 0's frame protocol in Tier 0's forms (push and pop, the
// frame pointer's and the base's `mov` in their RM64_R64 form, `sub`, `add` and `and` with an imm8 where the amount
// fits, the reset's `lea` from rbp), which unwind_tier0_frame_tails reads the transitions' PCs from (#177); a probe's
// loop moves rsp a page at a time, which no unwind table of that protocol describes.
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
//
// With a buffer's `events` (#1056), the emitter also appends the frame's events (emission_metadata.c's, in the x86-64
// unwind-register namespace), each at the record boundary after the instruction that makes the change, in the order
// the fixed-frame writer writes them (unwind.c): a push's CFA offset (while the CFA is rsp's) and its slot, rbp's
// pointing (the CFA rbp's), an allocation's CFA offset; a release's, a pop's and its register restored, rbp's pop's CFA
// back to rsp + 8. A probe's loop without a frame pointer (#1056's shape the fixed frame couldn't describe) has its CFA
// in r11 from the subtraction that sets r11 to the loop's bound, which no instruction of the loop changes, and back in
// rsp at the loop's exit, where rsp equals r11; with a frame pointer the CFA is rbp's and the probes move no rule. An
// epilogue's events are bracketed by a remembered state at its first record and a restored one after the ret that must
// follow it (both callers append the ret right after it), so that the body's rules hold again after an early return;
// a consumer drops the bracket of an epilogue whose ret ends the function (unwind_rows_canonical). An epilogue's state
// is the prologue's, replayed from the plan. A realignment without a frame pointer has no rule and is refused (INVALID)
// when events are asked for. Each step makes at most three events, and each epilogue two more.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#define BUSTER_TIER1_FRAME_EMIT_SCRATCH X86_REGISTER_R11
#define BUSTER_TIER1_FRAME_EMIT_RECORDS (BUSTER_TIER1_FRAME_STEP_CAPACITY * 12u)
#define BUSTER_TIER1_FRAME_EMIT_BRANCHES (BUSTER_TIER1_FRAME_STEP_CAPACITY * 2u)
#define BUSTER_TIER1_FRAME_EMIT_EVENTS (BUSTER_TIER1_FRAME_STEP_CAPACITY * 3u + 2u)

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
    // The frame's events (#1056), or null for none: each at a boundary of `records`.
    EmissionFrameEvent* events;
    uint32_t event_count;
    uint32_t event_capacity;
};

// Where the CFA is as the frame's steps run (#1056): `reg` (an encoder register: rsp, rbp, or r11 inside a probe's loop)
// plus `offset`, and how far rsp is below it (`depth`), unknown after a realignment until rsp is reset from rbp.
typedef struct Tier1FrameCfa Tier1FrameCfa;
struct Tier1FrameCfa
{
    uint32_t reg;
    int64_t offset;
    int64_t depth;
    bool depth_known;
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

// An immediate's form: the sign-extended imm8 where the value fits it, which the unwind tables' reader takes as the
// imm32 one (unwind_tier0_adjustment, unwind_tier0_realignment).
static uint16_t tier1_frame_emit_form(uint16_t imm8, uint16_t imm32, int64_t value)
{
    return value >= INT8_MIN && value <= INT8_MAX ? imm8 : imm32;
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

// The records of a probed allocation that change its CFA rule (#1056): r11's subtraction to the loop's bound, the loop's
// jne and the remainder's subtraction, BUSTER_TIER1_FRAME_NONE for those it hasn't.
typedef struct Tier1FrameProbeRecords Tier1FrameProbeRecords;
struct Tier1FrameProbeRecords
{
    uint32_t bound;
    uint32_t exit;
    uint32_t remainder;
};

static Tier1FrameProbeRecords tier1_frame_emit_probe_allocate(uint32_t amount, Tier1EmitBuffer* buffer)
{
    const uint32_t pages = amount & ~(BUSTER_TIER1_FRAME_PAGE_BYTES - 1);
    const uint32_t remainder = amount - pages;
    Tier1FrameProbeRecords positions = { BUSTER_TIER1_FRAME_NONE, BUSTER_TIER1_FRAME_NONE, BUSTER_TIER1_FRAME_NONE };
    if (pages != 0)
    {
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        positions.bound = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, BUSTER_TIER1_FRAME_EMIT_SCRATCH, (int32_t)pages));
        const uint32_t loop = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, BUSTER_TIER1_FRAME_PAGE_BYTES));
        tier1_frame_emit_touch(buffer);
        tier1_frame_emit_add(buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, BUSTER_TIER1_FRAME_EMIT_SCRATCH, X86_REGISTER_RSP));
        positions.exit = buffer->count;
        tier1_frame_emit_branch(buffer, X86_FORM_JNE_REL8, loop);
    }
    if (remainder != 0)
    {
        positions.remainder = buffer->count;
        tier1_frame_emit_add(buffer, tier1_frame_emit_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, (int32_t)remainder));
        tier1_frame_emit_touch(buffer);
    }
    return positions;
}

// An event at the boundary after record `record` of the step's temporary records (its LEFT side).
static void tier1_frame_emit_event(EmissionFrameEvent* events, uint32_t* count, uint32_t record, uint32_t kind, uint32_t reg, int64_t value)
{
    events[*count] = (EmissionFrameEvent){
        .at = { .boundary = record + 1, .side = EMISSION_LEFT },
        .value = (int32_t)value,
        .reg = (uint16_t)reg,
        .kind = (uint8_t)kind,
    };
    *count += 1;
}

// A step's effect on the CFA (Tier1FrameCfa) and, with `events`, its events at the records it wrote: the step's first
// `first`, its last `last` (the record before temporary.count), and a probe's (`probe`). Returns false for a step whose
// CFA no rule describes (a realignment while the CFA is rsp's, rbp pointed or rsp reset from an unknown depth, a CFA
// past int32's offsets).
static bool tier1_frame_emit_cfa(const Tier1FrameStep* step, Tier1FrameCfa* cfa, uint32_t last, Tier1FrameProbeRecords probe, EmissionFrameEvent* events,
                                 uint32_t* event_count)
{
    bool valid = true;
    const bool rsp = cfa->reg == X86_REGISTER_RSP;
    EmissionFrameEvent ignored[3];
    uint32_t ignored_count = 0;
    EmissionFrameEvent* out = events != 0 ? events : ignored;
    uint32_t* out_count = events != 0 ? event_count : &ignored_count;
    switch (step->kind)
    {
    case TIER1_FRAME_PUSH_POINTER:
    case TIER1_FRAME_PUSH_REGISTER:
        cfa->depth += 8;
        cfa->offset += rsp ? 8 : 0;
        if (rsp)
        {
            tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_CFA_OFFSET, 0, cfa->offset);
        }
        valid = cfa->depth_known;
        tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_OFFSET, emission_x86_registers[step->reg], -cfa->depth);
        break;
    case TIER1_FRAME_SET_POINTER:
        valid = rsp && cfa->depth_known;
        cfa->reg = X86_REGISTER_RBP;
        cfa->offset = cfa->depth;
        tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_CFA, BUSTER_EMISSION_X86_RBP, cfa->offset);
        break;
    case TIER1_FRAME_ALIGN_STACK:
    case TIER1_FRAME_PROBE_ALIGN:
        valid = !rsp;
        cfa->depth_known = false;
        break;
    case TIER1_FRAME_ALLOCATE:
    case TIER1_FRAME_PROBE_ALLOCATE:
    {
        const int64_t amount = step->amount;
        const uint32_t pages = step->kind == TIER1_FRAME_PROBE_ALLOCATE ? step->amount & ~(BUSTER_TIER1_FRAME_PAGE_BYTES - 1) : 0;
        if (rsp && pages != 0)
        {
            // r11 = rsp - pages, the loop's bound: the CFA is r11's through the loop, rsp's again where rsp reaches it.
            tier1_frame_emit_event(out, out_count, probe.bound, EMISSION_FRAME_CFA, emission_x86_registers[BUSTER_TIER1_FRAME_EMIT_SCRATCH], cfa->offset + pages);
            tier1_frame_emit_event(out, out_count, probe.exit, EMISSION_FRAME_CFA, BUSTER_EMISSION_X86_RSP, cfa->offset + pages);
        }
        cfa->depth += amount;
        cfa->offset += rsp ? amount : 0;
        if (rsp && amount != pages)
        {
            tier1_frame_emit_event(out, out_count, step->kind == TIER1_FRAME_PROBE_ALLOCATE ? probe.remainder : last, EMISSION_FRAME_CFA_OFFSET, 0, cfa->offset);
        }
        break;
    }
    case TIER1_FRAME_RELEASE:
        cfa->depth -= step->amount;
        cfa->offset -= rsp ? (int64_t)step->amount : 0;
        valid = cfa->depth_known;
        if (rsp)
        {
            tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_CFA_OFFSET, 0, cfa->offset);
        }
        break;
    case TIER1_FRAME_RESET_STACK:
        valid = cfa->reg == X86_REGISTER_RBP;
        cfa->depth = cfa->offset - step->offset;
        cfa->depth_known = true;
        break;
    case TIER1_FRAME_POP_REGISTER:
        cfa->depth -= 8;
        cfa->offset -= rsp ? 8 : 0;
        valid = cfa->depth_known;
        if (rsp)
        {
            tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_CFA_OFFSET, 0, cfa->offset);
        }
        tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_RESTORE, emission_x86_registers[step->reg], 0);
        break;
    case TIER1_FRAME_POP_POINTER:
        cfa->depth -= 8;
        valid = cfa->depth_known && cfa->depth == 8;
        cfa->reg = X86_REGISTER_RSP;
        cfa->offset = cfa->depth;
        tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_CFA, BUSTER_EMISSION_X86_RSP, cfa->offset);
        tier1_frame_emit_event(out, out_count, last, EMISSION_FRAME_RESTORE, BUSTER_EMISSION_X86_RBP, 0);
        break;
    case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
    case TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL:
    case TIER1_FRAME_SET_REALIGN_BASE:
        break;
    }
    valid = valid && cfa->offset >= 0 && cfa->offset <= INT32_MAX && (!cfa->depth_known || (cfa->depth >= 8 && cfa->depth <= INT32_MAX));
    if (events == 0)
    {
        ignored_count = 0;
    }
    return valid;
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
    case TIER1_FRAME_PUSH_REGISTER:
    case TIER1_FRAME_POP_REGISTER:
        valid &= step->reg < 16 && ((BUSTER_ABI_CALLEE_SAVED >> step->reg) & 1) != 0 && step->base == X86_REGISTER_RSP && step->amount == 8;
        valid &= epilogue == (step->kind == TIER1_FRAME_POP_REGISTER);
        break;
    case TIER1_FRAME_SET_POINTER:
        valid &= !epilogue && step->reg == X86_REGISTER_RBP && step->base == X86_REGISTER_RSP;
        break;
    case TIER1_FRAME_SET_REALIGN_BASE:
        valid &= !epilogue && step->reg == BUSTER_TIER1_FRAME_REALIGN_BASE && step->base == X86_REGISTER_RSP;
        break;
    case TIER1_FRAME_RESET_STACK:
        valid &= epilogue && step->reg == X86_REGISTER_RSP && step->base == X86_REGISTER_RBP && step->offset <= 0 && step->offset >= -8 * 6 && (step->offset & 7) == 0;
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
    case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
        valid &= !epilogue && step->reg < 16 && step->reg != X86_REGISTER_RSP && step->amount == 8;
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
    // The frame's events (#1056), at the temporary records' boundaries until they are copied, and the CFA as the steps
    // run: the CIE's at the prologue, and at an epilogue the prologue's, replayed.
    const bool described = error == TIER1_FRAME_EMIT_OK && buffer->events != 0;
    EmissionFrameEvent events[BUSTER_TIER1_FRAME_EMIT_EVENTS];
    uint32_t event_count = 0;
    Tier1FrameCfa cfa = { .reg = X86_REGISTER_RSP, .offset = 8, .depth = 8, .depth_known = true };
    if (error == TIER1_FRAME_EMIT_OK)
    {
        count = epilogue ? frame->epilogue_count : frame->prologue_count;
        steps = epilogue ? frame->epilogue : frame->prologue;
        if (count > BUSTER_TIER1_FRAME_STEP_CAPACITY || frame->prologue_count > BUSTER_TIER1_FRAME_STEP_CAPACITY || buffer->count > buffer->capacity ||
            buffer->branch_count > buffer->branch_capacity || (described && buffer->event_count > buffer->event_capacity))
        {
            error = TIER1_FRAME_EMIT_INVALID;
        }
    }
    const Tier1FrameProbeRecords none = { BUSTER_TIER1_FRAME_NONE, BUSTER_TIER1_FRAME_NONE, BUSTER_TIER1_FRAME_NONE };
    for (uint32_t i = 0; described && epilogue && error == TIER1_FRAME_EMIT_OK && i < frame->prologue_count; i += 1)
    {
        error = tier1_frame_emit_cfa(&frame->prologue[i], &cfa, 0, none, 0, 0) ? error : TIER1_FRAME_EMIT_INVALID;
    }
    if (described && epilogue)
    {
        // The body's rules, remembered at the epilogue's first record: restored after the ret that follows it.
        events[event_count++] = (EmissionFrameEvent){
            .at = { .boundary = 0, .side = EMISSION_LEFT },
            .kind = EMISSION_FRAME_REMEMBER_STATE,
        };
    }
    for (uint32_t i = 0; i < count && error == TIER1_FRAME_EMIT_OK; i += 1)
    {
        const Tier1FrameStep step = steps[i];
        const uint32_t step_first = temporary.count;
        Tier1FrameProbeRecords probe = none;
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
            case TIER1_FRAME_PUSH_REGISTER:
            case TIER1_FRAME_POP_REGISTER:
            {
                const bool push = step.kind == TIER1_FRAME_PUSH_POINTER || step.kind == TIER1_FRAME_PUSH_REGISTER;
                tier1_frame_emit_add(&temporary, tier1_frame_emit_register(push ? X86_FORM_PUSH_R64 : X86_FORM_POP_R64, 0, step.reg));
                break;
            }
            case TIER1_FRAME_SET_POINTER:
            case TIER1_FRAME_SET_REALIGN_BASE:
                // mov reg, rsp in Tier 0's form: rsp the ModRM reg operand's source.
                tier1_frame_emit_add(&temporary, tier1_frame_emit_register(X86_FORM_MOV_RM64_R64, step.base, step.reg));
                break;
            case TIER1_FRAME_RESET_STACK:
                // rsp back to the last push: `lea rsp, [rbp - 8n]`, or `mov rsp, rbp` with no pushes.
                tier1_frame_emit_add(&temporary, step.offset != 0 ? tier1_frame_emit_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RSP, step.base, step.offset) :
                                                                     tier1_frame_emit_register(X86_FORM_MOV_RM64_R64, step.base, X86_REGISTER_RSP));
                break;
            case TIER1_FRAME_ALIGN_STACK:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(tier1_frame_emit_form(X86_FORM_AND_RM64_IMM8, X86_FORM_AND_RM64_IMM32, -(int64_t)step.amount),
                                                                            X86_REGISTER_RSP, -(int32_t)step.amount));
                break;
            case TIER1_FRAME_PROBE_ALIGN:
                tier1_frame_emit_probe_align(step.amount, &temporary);
                break;
            case TIER1_FRAME_ALLOCATE:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(tier1_frame_emit_form(X86_FORM_SUB_RM64_IMM8, X86_FORM_SUB_RM64_IMM32, step.amount), X86_REGISTER_RSP, (int32_t)step.amount));
                break;
            case TIER1_FRAME_RELEASE:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_immediate(tier1_frame_emit_form(X86_FORM_ADD_RM64_IMM8, X86_FORM_ADD_RM64_IMM32, step.amount), X86_REGISTER_RSP, (int32_t)step.amount));
                break;
            case TIER1_FRAME_PROBE_ALLOCATE:
                probe = tier1_frame_emit_probe_allocate(step.amount, &temporary);
                break;
            case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
                tier1_frame_emit_add(&temporary, tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, step.reg, step.base, step.offset));
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
        if (error == TIER1_FRAME_EMIT_OK && described)
        {
            // The step's own records: a step that writes none (none does) makes no event.
            error = temporary.count > step_first && tier1_frame_emit_cfa(&step, &cfa, temporary.count - 1, probe, events, &event_count) ? error : TIER1_FRAME_EMIT_INVALID;
        }
    }
    if (described && epilogue)
    {
        if (event_count == 1)
        {
            // An epilogue that moves no rule (a frameless function's) remembers nothing.
            event_count = 0;
        }
        else
        {
            // After the ret, the record after the epilogue's last.
            events[event_count++] = (EmissionFrameEvent){
                .at = { .boundary = temporary.count + 1, .side = EMISSION_LEFT },
                .kind = EMISSION_FRAME_RESTORE_STATE,
            };
        }
    }
    if (error == TIER1_FRAME_EMIT_OK)
    {
        if (temporary.count > buffer->capacity - buffer->count || temporary.branch_count > buffer->branch_capacity - buffer->branch_count ||
            (described && event_count > buffer->event_capacity - buffer->event_count))
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
        for (uint32_t i = 0; described && i < event_count; i += 1)
        {
            EmissionFrameEvent event = events[i];
            event.at.boundary += buffer->count;
            buffer->events[buffer->event_count + i] = event;
        }
        buffer->count += temporary.count;
        buffer->branch_count += temporary.branch_count;
        buffer->event_count += described ? event_count : 0;
    }
    return error;
}
