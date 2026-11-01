// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Included after tier1_frame.c. Independent pairwise overlap/alignment and pressure checks validate both colorings;
// a sparse abstract stack executes the planned prologue/epilogue and checks register restoration, including dynamic
// realigned frames. This checks plans, not generated code, unwind PCs, Clang frame sizes or Tier 1 cross-linking.

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_TIER1_FRAME_TEST_ARENA_BYTES (16u << 20)

typedef struct Tier1FrameTest Tier1FrameTest;
struct Tier1FrameTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    uint32_t count;
    uint32_t shape;
};

static void tier1_frame_test_expect(Tier1FrameTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 12)
        {
            fprintf(stderr, "tier1-frame: %s (seed=%" PRIu64 ", count=%u, shape=%u)\n", description, test->seed, test->count, test->shape);
        }
        test->failures += 1;
    }
}

static uint32_t tier1_frame_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return (uint32_t)(*state >> 16);
}

// Independent of the planner's class formation, event order and coloring. A maximum occurs at an interval start.
static void tier1_frame_test_valid(Tier1FrameTest* test, const Tier1FrameRequest* request, const Tier1Frame* frame)
{
    const Tier1FrameSlots* input = &request->slots;
    uint64_t minimum_bytes = 0;
    uint32_t minimum_slots = 0;
    uint32_t classes = 0;
    // A red zone's base is rsp after the pushes: entry RSP (8 modulo 16) less 8 per push.
    const int64_t saves = 8 * (int64_t)__builtin_popcount(frame->saved_registers);
    const int64_t base_residue = frame->red_zone ? (8 + saves) % 16 : 0;
    for (uint32_t i = 0; i < input->count; i += 1)
    {
        const uint32_t row = input->indices != 0 ? input->indices[i] : i;
        const uint64_t alignment = input->alignment[row];
        const uint64_t stride = ((uint64_t)input->size[row] + alignment - 1) / alignment * alignment;
        const int64_t begin = frame->offset[i];
        const int64_t end = begin + (int64_t)stride;
        tier1_frame_test_expect(test, (base_residue + begin) % (int64_t)alignment == 0, "aligned object displacement");
        tier1_frame_test_expect(test, frame->slot[i] < frame->slot_count, "bounded physical slot ID");
        if (frame->red_zone)
        {
            tier1_frame_test_expect(test, begin >= -(int64_t)frame->red_zone_bytes && end <= 0, "object inside red zone");
        }
        else if (frame->base_register == BUSTER_ABI_RBP)
        {
            tier1_frame_test_expect(test, begin >= -(int64_t)frame->stack_adjust_bytes - saves && end <= -saves, "dynamic fixed object inside RBP extent, below the pushes");
        }
        else
        {
            tier1_frame_test_expect(test, begin >= 0 && end <= frame->stack_adjust_bytes, "object inside allocated stack");
        }
        if (!frame->dynamic_outgoing && frame->outgoing_bytes != 0)
        {
            tier1_frame_test_expect(test, begin >= frame->outgoing_bytes, "objects after outgoing arguments");
        }
        bool first_class = true;
        for (uint32_t j = 0; j < input->count; j += 1)
        {
            const uint32_t other = input->indices != 0 ? input->indices[j] : j;
            const uint64_t other_alignment = input->alignment[other];
            const uint64_t other_stride = ((uint64_t)input->size[other] + other_alignment - 1) / other_alignment * other_alignment;
            const bool same_class = alignment == other_alignment && stride == other_stride;
            first_class &= j >= i || !same_class;
            if (j < i)
            {
                const bool lifetime_overlap = input->start[row] < input->end[other] && input->start[other] < input->end[row];
                const bool byte_overlap = begin < (int64_t)frame->offset[j] + (int64_t)other_stride && frame->offset[j] < end;
                tier1_frame_test_expect(test, !lifetime_overlap || !byte_overlap, "live object byte ranges do not overlap");
                tier1_frame_test_expect(test, frame->slot[i] != frame->slot[j] || (same_class && !lifetime_overlap && begin == frame->offset[j]),
                    "shared slot has equal storage class and disjoint lifetime");
            }
        }
        if (first_class)
        {
            uint32_t maximum = 0;
            for (uint32_t at = 0; at < input->count; at += 1)
            {
                const uint32_t at_row = input->indices != 0 ? input->indices[at] : at;
                const uint32_t position = input->start[at_row];
                uint32_t pressure = 0;
                for (uint32_t j = 0; j < input->count; j += 1)
                {
                    const uint32_t other = input->indices != 0 ? input->indices[j] : j;
                    const uint64_t other_alignment = input->alignment[other];
                    const uint64_t other_stride = ((uint64_t)input->size[other] + other_alignment - 1) / other_alignment * other_alignment;
                    pressure += other_alignment == alignment && other_stride == stride && input->start[other] <= position && position < input->end[other];
                }
                maximum = maximum > pressure ? maximum : pressure;
            }
            classes += 1;
            minimum_slots += maximum;
            minimum_bytes += maximum * stride;
        }
    }
    tier1_frame_test_expect(test, frame->slot_count == minimum_slots && frame->class_count == classes, "color count equals independent interval pressure lower bound");
    tier1_frame_test_expect(test, frame->object_bytes == minimum_bytes, "class bytes equal independent optimum");
    tier1_frame_test_expect(test, !frame->red_zone || (frame->red_zone_bytes <= 128 && frame->stack_adjust_bytes == 0 && request->call_count == 0), "red-zone eligibility");
    tier1_frame_test_expect(test, !frame->realigned || frame->frame_pointer, "realignment retains frame pointer");
    tier1_frame_test_expect(test, (request->flags & BUSTER_TIER1_FRAME_DYNAMIC_STACK) == 0 || frame->frame_pointer, "dynamic stack retains frame pointer");
    tier1_frame_test_expect(test, frame->maximum_stack_bytes <= INT32_MAX, "bounded stack extent");
    // Entry RSP is 8 modulo 16; the return address, rbp's push and the callee-saved registers' pushes come before the
    // subtraction, after which rsp is 16-aligned (and a realigned frame's `and` aligns it further).
    const uint64_t pushed = 8 + 8 * (uint64_t)__builtin_popcount(frame->saved_registers) + (frame->frame_pointer ? 8 : 0);
    if (!frame->red_zone && (frame->payload_bytes != 0 || request->call_count != 0))
    {
        tier1_frame_test_expect(test, (frame->stack_adjust_bytes + pushed) % 16 == 0, "stack alignment after prologue");
    }
    // The steps are Tier 0's frame protocol, which the unwind tables describe: rbp's push and mov, the registers' pushes
    // in ascending order, one subtraction, the realignment and its base's copy; then the release (or the reset from rbp)
    // and the pops in reverse.
    uint32_t order = 0;
    uint32_t phase = 0;
    uint32_t last_push = 0;
    for (uint32_t i = 0; i < frame->prologue_count; i += 1)
    {
        const Tier1FrameStep* step = &frame->prologue[i];
        const uint32_t step_phase = step->kind == TIER1_FRAME_PUSH_POINTER ? 0 : step->kind == TIER1_FRAME_SET_POINTER ? 1 : step->kind == TIER1_FRAME_PUSH_REGISTER ? 2 :
                                    step->kind == TIER1_FRAME_ALLOCATE || step->kind == TIER1_FRAME_PROBE_ALLOCATE ? 3 :
                                    step->kind == TIER1_FRAME_ALIGN_STACK || step->kind == TIER1_FRAME_PROBE_ALIGN ? 4 : step->kind == TIER1_FRAME_SET_REALIGN_BASE ? 5 : 6;
        order += step_phase < phase || (step_phase == 2 && phase == 2 && step->reg <= last_push);
        phase = step_phase;
        last_push = step->kind == TIER1_FRAME_PUSH_REGISTER ? step->reg : last_push;
    }
    tier1_frame_test_expect(test, order == 0, "prologue follows the unwind tables' protocol");
    const uint32_t pops = (uint32_t)__builtin_popcount(frame->saved_registers);
    const uint32_t release = frame->frame_pointer || frame->stack_adjust_bytes != 0 ? 1 : 0;
    bool epilogue = frame->epilogue_count == release + pops + (frame->frame_pointer ? 1 : 0);
    epilogue = epilogue && (release == 0 || frame->epilogue[0].kind == (frame->frame_pointer ? TIER1_FRAME_RESET_STACK : TIER1_FRAME_RELEASE));
    epilogue = epilogue && (!frame->frame_pointer || (frame->epilogue[0].offset == -8 * (int32_t)pops && frame->epilogue[frame->epilogue_count - 1].kind == TIER1_FRAME_POP_POINTER));
    for (uint32_t k = 0; epilogue && k < pops; k += 1)
    {
        const Tier1FrameStep* step = &frame->epilogue[release + k];
        epilogue = step->kind == TIER1_FRAME_POP_REGISTER && (k == 0 || step->reg < frame->epilogue[release + k - 1].reg);
    }
    tier1_frame_test_expect(test, epilogue, "epilogue follows the unwind tables' protocol");
}

typedef struct Tier1FrameTestMemory Tier1FrameTestMemory;
struct Tier1FrameTestMemory
{
    uint64_t address;
    uint64_t value;
    uint32_t bytes;
};

static void tier1_frame_test_steps(Tier1FrameTest* test, const Tier1FrameRequest* request, const Tier1Frame* frame, uint32_t entry_al)
{
    uint64_t registers[16];
    uint64_t original[16];
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        original[reg] = 0xabc00000 + reg * 97;
        registers[reg] = original[reg];
    }
    // Different entry residues modulo over-alignment exercise the unknown realignment gap.
    const uint64_t entry_rsp = 0x400000008ull + (test->shape % 16) * 16;
    original[BUSTER_ABI_RSP] = entry_rsp;
    registers[BUSTER_ABI_RSP] = entry_rsp;
    Tier1FrameTestMemory memory[32];
    uint32_t writes = 0;
    uint32_t vector_saves = 0;
    for (uint32_t i = 0; i < frame->prologue_count; i += 1)
    {
        const Tier1FrameStep step = frame->prologue[i];
        bool write = false;
        uint64_t value = 0;
        uint64_t address = registers[step.base] + step.offset;
        switch (step.kind)
        {
            case TIER1_FRAME_PUSH_POINTER:
            case TIER1_FRAME_PUSH_REGISTER:
            {
                registers[BUSTER_ABI_RSP] -= 8;
                address = registers[BUSTER_ABI_RSP];
                value = registers[step.reg];
                write = true;
                // A saved register's slot, as the plan publishes it: from rbp with a frame pointer, else from the
                // post-prologue rsp, which the steps after it reach (checked below).
                if (step.kind == TIER1_FRAME_PUSH_REGISTER && frame->frame_pointer)
                {
                    tier1_frame_test_expect(test, address == registers[BUSTER_ABI_RBP] + (uint64_t)(int64_t)frame->save_offset[step.reg], "a push writes its published slot");
                }
            } break;
            case TIER1_FRAME_SET_POINTER:
            case TIER1_FRAME_SET_REALIGN_BASE:
            {
                registers[step.reg] = registers[step.base];
            } break;
            case TIER1_FRAME_ALIGN_STACK:
            case TIER1_FRAME_PROBE_ALIGN:
            {
                registers[BUSTER_ABI_RSP] &= ~((uint64_t)step.amount - 1);
            } break;
            case TIER1_FRAME_ALLOCATE:
            case TIER1_FRAME_PROBE_ALLOCATE:
            {
                registers[BUSTER_ABI_RSP] -= step.amount;
            } break;
            case TIER1_FRAME_SAVE_VARIADIC_INTEGER:
            {
                value = registers[step.reg];
                write = true;
            } break;
            case TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL:
            {
                write = entry_al != 0;
                value = 0xdef00000 + step.reg;
                vector_saves += write;
                tier1_frame_test_expect(test, address % 16 == 0, "variadic vector save is aligned");
            } break;
            default:
            {
                tier1_frame_test_expect(test, false, "valid prologue step");
            } break;
        }
        if (write)
        {
            tier1_frame_test_expect(test, writes < 32, "bounded abstract stack writes");
            for (uint32_t j = 0; j < writes; j += 1)
            {
                tier1_frame_test_expect(test, address + step.amount <= memory[j].address || memory[j].address + memory[j].bytes <= address,
                    "prologue save ranges do not overlap");
            }
            if (writes < 32)
            {
                memory[writes] = (Tier1FrameTestMemory){
                    .address = address,
                    .value = value,
                    .bytes = step.amount,
                };
                writes += 1;
            }
        }
    }
    tier1_frame_test_expect(test, vector_saves == (frame->variadic && entry_al != 0 ? 8u : 0u), "entry AL controls all vector save steps");
    for (uint32_t reg = 0; !frame->frame_pointer && reg < 16; reg += 1)
    {
        if ((frame->saved_registers & (1u << reg)) != 0)
        {
            bool found = false;
            for (uint32_t j = 0; j < writes; j += 1)
            {
                found |= memory[j].address == registers[BUSTER_ABI_RSP] + (uint64_t)(int64_t)frame->save_offset[reg] && memory[j].value == original[reg];
            }
            tier1_frame_test_expect(test, found, "a push writes its published slot");
        }
    }
    for (uint32_t i = 0; i < request->slots.count; i += 1)
    {
        const uint32_t row = request->slots.indices != 0 ? request->slots.indices[i] : i;
        const uint64_t address = registers[frame->base_register] + frame->offset[i];
        tier1_frame_test_expect(test, address % request->slots.alignment[row] == 0, "absolute object address aligned after simulated prologue");
        for (uint32_t j = 0; j < writes; j += 1)
        {
            tier1_frame_test_expect(test, address + request->slots.size[row] <= memory[j].address || memory[j].address + memory[j].bytes <= address,
                "objects do not overlap saved state");
        }
    }
    if (request->call_count != 0)
    {
        tier1_frame_test_expect(test, registers[BUSTER_ABI_RSP] % frame->alignment == 0, "absolute RSP meets outgoing alignment");
    }
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        if ((request->used_callee_saved & (1u << reg)) != 0)
        {
            registers[reg] = 0xdead0000 + reg;
        }
    }
    if ((request->flags & BUSTER_TIER1_FRAME_DYNAMIC_STACK) != 0)
    {
        registers[BUSTER_ABI_RSP] -= 8192;
    }
    for (uint32_t i = 0; i < frame->epilogue_count; i += 1)
    {
        const Tier1FrameStep step = frame->epilogue[i];
        switch (step.kind)
        {
            case TIER1_FRAME_POP_REGISTER:
            case TIER1_FRAME_POP_POINTER:
            {
                const uint64_t address = registers[step.base] + step.offset;
                bool found = false;
                for (uint32_t j = 0; j < writes; j += 1)
                {
                    if (memory[j].address == address)
                    {
                        registers[step.reg] = memory[j].value;
                        found = true;
                    }
                }
                tier1_frame_test_expect(test, found, "restoration addresses a saved value");
                registers[BUSTER_ABI_RSP] += 8;
            } break;
            case TIER1_FRAME_RELEASE:
            {
                registers[BUSTER_ABI_RSP] += step.amount;
            } break;
            case TIER1_FRAME_RESET_STACK:
            {
                registers[BUSTER_ABI_RSP] = registers[step.base] + (uint64_t)(int64_t)step.offset;
            } break;
            default:
            {
                tier1_frame_test_expect(test, false, "valid epilogue step");
            } break;
        }
    }
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        if (((BUSTER_ABI_CALLEE_SAVED | (1u << BUSTER_ABI_RSP)) & (1u << reg)) != 0)
        {
            tier1_frame_test_expect(test, registers[reg] == original[reg], "prologue/epilogue preserve every callee save and RSP");
        }
    }
}

static bool tier1_frame_test_same_metadata(const Tier1Frame* left, const Tier1Frame* right)
{
    const bool same = left->count == right->count && left->slot_count == right->slot_count && left->class_count == right->class_count &&
        left->base_register == right->base_register && left->alignment == right->alignment && left->object_bytes == right->object_bytes &&
        left->payload_bytes == right->payload_bytes && left->stack_adjust_bytes == right->stack_adjust_bytes && left->maximum_stack_bytes == right->maximum_stack_bytes &&
        left->red_zone_bytes == right->red_zone_bytes && left->outgoing_bytes == right->outgoing_bytes && left->dynamic_outgoing == right->dynamic_outgoing &&
        left->frame_pointer == right->frame_pointer && left->realigned == right->realigned && left->red_zone == right->red_zone && left->variadic == right->variadic &&
        left->variadic_offset == right->variadic_offset && left->saved_registers == right->saved_registers && left->prologue_count == right->prologue_count &&
        left->epilogue_count == right->epilogue_count && memcmp(left->save_offset, right->save_offset, sizeof(left->save_offset)) == 0 &&
        memcmp(left->prologue, right->prologue, left->prologue_count * sizeof(Tier1FrameStep)) == 0 &&
        memcmp(left->epilogue, right->epilogue, left->epilogue_count * sizeof(Tier1FrameStep)) == 0;
    return same;
}

static void tier1_frame_test_case(Tier1FrameTest* test, const Tier1FrameRequest* request, Arena* output, Arena* scratch, bool execute)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1Frame frame[2];
    const Tier1FrameError vector_error = tier1_frame_plan(request, output, scratch, &frame[0]);
    const Tier1FrameError scalar_error = tier1_frame_plan_scalar(request, output, scratch, &frame[1]);
    tier1_frame_test_expect(test, vector_error == TIER1_FRAME_OK && scalar_error == TIER1_FRAME_OK, "both paths accept valid frame");
    tier1_frame_test_expect(test, scratch->length == scratch_mark, "scratch restored on success");
    if (vector_error == TIER1_FRAME_OK && scalar_error == TIER1_FRAME_OK)
    {
        tier1_frame_test_expect(test, tier1_frame_test_same_metadata(&frame[0], &frame[1]), "scalar and vector frame sizes and plans agree");
        tier1_frame_test_valid(test, request, &frame[0]);
        tier1_frame_test_valid(test, request, &frame[1]);
        if (execute)
        {
            tier1_frame_test_steps(test, request, &frame[0], 0);
            tier1_frame_test_steps(test, request, &frame[1], 8);
        }
        Tier1Frame repeated;
        const Tier1FrameError repeated_error = tier1_frame_plan(request, output, scratch, &repeated);
        tier1_frame_test_expect(test, repeated_error == TIER1_FRAME_OK && tier1_frame_test_same_metadata(&frame[0], &repeated), "repeatable plan metadata");
        if (repeated_error == TIER1_FRAME_OK && request->slots.count != 0)
        {
            tier1_frame_test_expect(test, memcmp(frame[0].slot, repeated.slot, request->slots.count * 4) == 0 &&
                memcmp(frame[0].offset, repeated.offset, request->slots.count * 4) == 0, "repeatable slot identities and offsets");
        }
        if (request->slots.indices != 0 && request->slots.count == request->slots.column_count)
        {
            Tier1FrameRequest identity = *request;
            identity.slots.indices = 0;
            Tier1Frame reordered;
            const Tier1FrameError reordered_error = tier1_frame_plan(&identity, output, scratch, &reordered);
            tier1_frame_test_expect(test, reordered_error == TIER1_FRAME_OK && tier1_frame_test_same_metadata(&frame[0], &reordered),
                "legal input permutation preserves optimal frame extent");
        }
    }
    arena_pop_to(output, output_mark);
}

static void tier1_frame_test_refusal(Tier1FrameTest* test, const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1FrameError expected)
{
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        Tier1Frame before;
        memset(&before, 0xa5, sizeof(before));
        Tier1Frame result = before;
        const uint64_t output_mark = output->length;
        const uint64_t scratch_mark = scratch->length;
        const Tier1FrameError error = scalar ? tier1_frame_plan_scalar(request, output, scratch, &result) : tier1_frame_plan(request, output, scratch, &result);
        tier1_frame_test_expect(test, error == expected, "expected refusal status");
        tier1_frame_test_expect(test, memcmp(&before, &result, sizeof(result)) == 0, "refusal preserves result");
        tier1_frame_test_expect(test, output_mark == output->length && scratch_mark == scratch->length, "refusal restores both arenas");
    }
}

static void tier1_frame_test_limits(Tier1FrameTest* test, Arena* output, Arena* scratch)
{
    uint32_t starts[2] = {0, 0};
    uint32_t ends[2] = {4, 4};
    uint32_t sizes[2] = {8, 8};
    uint32_t alignments[2] = {8, 8};
    uint32_t indices[2] = {0, 1};
    Tier1FrameRequest request = {
        .slots = {
            .start = starts,
            .end = ends,
            .size = sizes,
            .alignment = alignments,
            .indices = indices,
            .count = 2,
            .column_count = 2,
        },
    };
    test->count = 2;
    for (uint32_t mutation = 0; mutation < 13; mutation += 1)
    {
        test->shape = mutation;
        Tier1FrameError error = TIER1_FRAME_INVALID;
        switch (mutation)
        {
            case 0: sizes[0] = 0; break;
            case 1: alignments[0] = 0; break;
            case 2: alignments[0] = 3; break;
            case 3: alignments[0] = 1u << 31; break;
            case 4: starts[0] = 4; break;
            case 5: ends[0] = 0; break;
            case 6: indices[1] = 0; break;
            case 7: indices[1] = 2; break;
            case 8: sizes[0] = UINT32_MAX; error = TIER1_FRAME_OVERFLOW; break;
            case 9: sizes[0] = INT32_MAX; error = TIER1_FRAME_OVERFLOW; break;
            case 10: request.flags = UINT32_MAX; break;
            case 11: request.used_callee_saved = 1u << BUSTER_ABI_RSP; break;
            case 12: request.slots.count = BUSTER_TIER1_FRAME_MAX_OBJECTS + 1; break;
        }
        tier1_frame_test_refusal(test, &request, output, scratch, error);
        sizes[0] = 8;
        alignments[0] = 8;
        starts[0] = 0;
        ends[0] = 4;
        indices[1] = 1;
        request.flags = 0;
        request.used_callee_saved = 0;
        request.slots.count = 2;
    }
    sizes[0] = 1u << 30;
    sizes[1] = 1u << 30;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_OVERFLOW);
    sizes[0] = 8;
    sizes[1] = 8;
    request.flags = BUSTER_TIER1_FRAME_KEEP_POINTER;
    request.used_callee_saved = 1u << BUSTER_ABI_RBP;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_REGISTER_CONFLICT);
    request.flags = BUSTER_TIER1_FRAME_DYNAMIC_STACK;
    alignments[0] = 64;
    request.used_callee_saved = 1u << BUSTER_TIER1_FRAME_REALIGN_BASE;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_REGISTER_CONFLICT);
    request.used_callee_saved = 0;
    Tier1FrameRequirements requirements;
    tier1_frame_test_expect(test, tier1_frame_requirements(&request, &requirements) == TIER1_FRAME_OK &&
        requirements.reserved_registers == ((1u << BUSTER_ABI_RBP) | (1u << BUSTER_TIER1_FRAME_REALIGN_BASE)), "pre-allocation query reserves both stable bases");
    alignments[0] = 8;
    request.flags = 0;
    const uint64_t output_capacity = output->capacity;
    output->capacity = output->length + 15;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_CAPACITY);
    output->capacity = output_capacity;
    const uint64_t scratch_capacity = scratch->capacity;
    scratch->capacity = scratch->length + 32;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_CAPACITY);
    scratch->capacity = scratch_capacity;
    tier1_frame_test_refusal(test, &request, output, output, TIER1_FRAME_INVALID);
    AbiCall call = {
        .stack_alignment_log2 = 3,
    };
    request.calls = &call;
    request.call_count = 1;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_INVALID);
    call.stack_alignment_log2 = 4;
    call.stack_bytes = 3;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_INVALID);
    call.stack_bytes = 0xfffffff8u;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_OVERFLOW);
    call.stack_bytes = 0;
    call.stack_alignment_log2 = 32;
    tier1_frame_test_refusal(test, &request, output, scratch, TIER1_FRAME_INVALID);
}

static void tier1_frame_test_guards(Tier1FrameTest* test, Arena* output, Arena* scratch)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const uint32_t counts[] = {0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65};
    for (uint32_t case_index = 0; case_index < sizeof(counts) / sizeof(counts[0]); case_index += 1)
    {
        const uint32_t count = counts[case_index];
        uint8_t* mappings[5] = {0};
        uint32_t* columns[5] = {0};
        bool ready = true;
        for (uint32_t column = 0; column < 5; column += 1)
        {
            mappings[column] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mappings[column] == MAP_FAILED)
            {
                mappings[column] = 0;
                ready = false;
            }
            else
            {
                ready &= mprotect(mappings[column] + page, page, PROT_NONE) == 0;
                columns[column] = (uint32_t*)(mappings[column] + page - count * 4);
            }
        }
        tier1_frame_test_expect(test, ready, "guard mappings available");
        if (ready)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                columns[0][i] = i * 3;
                columns[1][i] = i * 3 + 5;
                columns[2][i] = 1 + i % 33;
                columns[3][i] = 1u << (i % 7);
                columns[4][i] = count - i - 1;
            }
            Tier1FrameRequest request = {
                .slots = {
                    .start = columns[0],
                    .end = columns[1],
                    .size = columns[2],
                    .alignment = columns[3],
                    .count = count,
                    .column_count = count,
                },
            };
            test->count = count;
            tier1_frame_test_case(test, &request, output, scratch, true);
            request.slots.indices = columns[4];
            tier1_frame_test_case(test, &request, output, scratch, true);
            if (count != 0)
            {
                uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                tier1_frame_test_expect(test, mapping != MAP_FAILED, "guarded output mapping available");
                if (mapping != MAP_FAILED)
                {
                    const bool protected = mprotect(mapping + page, page, PROT_NONE) == 0;
                    tier1_frame_test_expect(test, protected, "guarded output protected");
                    Arena guarded = {
                        .base = mapping + page - count * 8,
                        .capacity = count * 8,
                    };
                    Tier1Frame frame;
                    const Tier1FrameError error = tier1_frame_plan(&request, &guarded, scratch, &frame);
                    tier1_frame_test_expect(test, error == TIER1_FRAME_OK && guarded.length == guarded.capacity, "exact-capacity guarded output");
                    munmap(mapping, page * 2);
                }
            }
        }
        for (uint32_t column = 0; column < 5; column += 1)
        {
            if (mappings[column] != 0)
            {
                munmap(mappings[column], page * 2);
            }
        }
    }
}

static void tier1_frame_test_boundaries(Tier1FrameTest* test, Arena* output, Arena* scratch)
{
    const uint32_t start = 0;
    const uint32_t end = UINT32_MAX;
    uint32_t size = 128;
    uint32_t alignment = 8;
    Tier1FrameRequest request = {
        .slots = {
            .start = &start,
            .end = &end,
            .size = &size,
            .alignment = &alignment,
            .count = 1,
            .column_count = 1,
        },
    };
    test->count = 1;
    for (uint32_t shape = 0; shape < 10; shape += 1)
    {
        test->shape = shape;
        size = shape < 2 ? 128 : (shape < 4 ? 112 : (shape < 6 ? 4095 : 4097));
        alignment = shape & 1 ? 16 : 8;
        request.flags = shape >= 4 ? BUSTER_TIER1_FRAME_PROBE_STACK : 0;
        tier1_frame_test_case(test, &request, output, scratch, true);
        Tier1Frame frame;
        const uint64_t mark = output->length;
        const Tier1FrameError error = tier1_frame_plan(&request, output, scratch, &frame);
        tier1_frame_test_expect(test, error == TIER1_FRAME_OK, "boundary frame accepted");
        if (error == TIER1_FRAME_OK)
        {
            tier1_frame_test_expect(test, frame.red_zone == (shape == 0 || shape == 2 || shape == 3), "red-zone boundary accounts for entry RSP residue");
            if (shape >= 4)
            {
                bool probe = false;
                for (uint32_t i = 0; i < frame.prologue_count; i += 1)
                {
                    probe |= frame.prologue[i].kind == TIER1_FRAME_PROBE_ALLOCATE;
                }
                tier1_frame_test_expect(test, probe, "page-sized allocation plans probes");
            }
        }
        arena_pop_to(output, mark);
    }
    size = 19;
    alignment = 8192;
    request.flags = BUSTER_TIER1_FRAME_PROBE_STACK | BUSTER_TIER1_FRAME_DYNAMIC_STACK | BUSTER_TIER1_FRAME_VARIADIC;
    // R15 is this dynamic realigned frame's base (BUSTER_TIER1_FRAME_REALIGN_BASE).
    request.used_callee_saved = (1u << BUSTER_ABI_RBX) | (1u << BUSTER_ABI_R12) | (1u << BUSTER_ABI_R13) | (1u << BUSTER_ABI_R14);
    AbiCall calls[3] = {
        {.stack_bytes = 8, .stack_alignment_log2 = 4},
        {.stack_bytes = 40, .stack_alignment_log2 = 6},
        {.stack_bytes = 24, .stack_alignment_log2 = 5},
    };
    request.calls = calls;
    request.call_count = 3;
    tier1_frame_test_case(test, &request, output, scratch, true);
    Tier1FrameRequirements requirements;
    tier1_frame_test_expect(test, tier1_frame_requirements(&request, &requirements) == TIER1_FRAME_OK &&
        requirements.outgoing_bytes == 40 && requirements.alignment == 8192, "largest outgoing area and object alignment retained");
    size = 33;
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        alignment = shape & 1 ? 64 : 8;
        request.call_count = shape & 2 ? 1 : 0;
        test->shape = shape;
        for (uint32_t flags = 0; flags <= BUSTER_TIER1_FRAME_FLAGS; flags += 1)
        {
            request.flags = flags;
            request.used_callee_saved = 0;
            const Tier1FrameError error = tier1_frame_requirements(&request, &requirements);
            tier1_frame_test_expect(test, error == TIER1_FRAME_OK, "all valid policy combinations have pre-allocation requirements");
            if (error == TIER1_FRAME_OK)
            {
                request.used_callee_saved = BUSTER_ABI_CALLEE_SAVED & ~requirements.reserved_registers;
                tier1_frame_test_case(test, &request, output, scratch, true);
            }
        }
    }
}

static int run_tier1_frame_tests(int argc, char** argv)
{
    Tier1FrameTest test = {
        .seed = 177,
    };
    uint32_t rounds = 8;
    bool options = true;
    for (int i = 1; options && i < argc; i += 1)
    {
        const bool seed = strcmp(argv[i], "--seed") == 0;
        const bool count = strcmp(argv[i], "--count") == 0;
        if ((seed || count) && i + 1 < argc)
        {
            char* end;
            i += 1;
            const unsigned long long value = strtoull(argv[i], &end, 10);
            options = *argv[i] != 0 && *end == 0 && value != 0 && (seed || value <= 1000);
            if (seed)
            {
                test.seed = value;
            }
            else
            {
                rounds = (uint32_t)value;
            }
        }
        else
        {
            options = false;
        }
    }
    Arena output = arena_reserve(BUSTER_TIER1_FRAME_TEST_ARENA_BYTES);
    Arena scratch = arena_reserve(BUSTER_TIER1_FRAME_TEST_ARENA_BYTES);
    tier1_frame_test_expect(&test, options && output.capacity != 0 && scratch.capacity != 0, "valid options and test arenas");
    if (options && output.capacity != 0 && scratch.capacity != 0)
    {
        const uint32_t counts[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 257};
        uint32_t columns[5][257];
        uint64_t state = test.seed;
        for (uint32_t round = 0; round < rounds; round += 1)
        {
            for (uint32_t ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ci += 1)
            {
                const uint32_t count = counts[ci];
                test.count = count;
                test.shape = round;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t random = tier1_frame_test_random(&state);
                    const uint32_t shape = round % 8;
                    columns[0][i] = shape == 0 ? 0 : (shape == 1 ? i * 3 : (shape == 2 ? i : random % 64));
                    columns[1][i] = shape == 0 ? 1 : (shape == 1 ? i * 3 + 3 : (shape == 2 ? count * 2 - i : columns[0][i] + 1 + random % 33));
                    if (shape == 3)
                    {
                        columns[0][i] += UINT32_MAX - 128;
                        columns[1][i] += UINT32_MAX - 128;
                    }
                    columns[3][i] = shape < 4 ? 8 : 1u << (random % 7);
                    columns[2][i] = shape < 4 ? 8 : 1 + (random >> 8) % 97;
                    columns[4][i] = i;
                }
                for (uint32_t i = count; i > 1; i -= 1)
                {
                    const uint32_t j = tier1_frame_test_random(&state) % i;
                    const uint32_t swap = columns[4][j];
                    columns[4][j] = columns[4][i - 1];
                    columns[4][i - 1] = swap;
                }
                AbiCall call = {
                    .stack_bytes = round & 1 ? 24 : 0,
                    .stack_alignment_log2 = round % 3 == 0 ? 6 : 4,
                };
                Tier1FrameRequest request = {
                    .slots = {
                        .start = columns[0],
                        .end = columns[1],
                        .size = columns[2],
                        .alignment = columns[3],
                        .indices = round & 1 ? columns[4] : 0,
                        .count = count,
                        .column_count = count,
                    },
                    .calls = &call,
                    .call_count = round >= 4 ? 1 : 0,
                    // R15 is a dynamic realigned frame's base (BUSTER_TIER1_FRAME_REALIGN_BASE), which allocation can't use.
                    .used_callee_saved = (round & 2) != 0 ? (1u << BUSTER_ABI_RBX) | (1u << BUSTER_ABI_R13) | ((round & 4) == 0 ? 1u << BUSTER_ABI_R15 : 0) : 0,
                    .flags = (round & 4) != 0 ? BUSTER_TIER1_FRAME_VARIADIC | BUSTER_TIER1_FRAME_DYNAMIC_STACK : 0,
                };
                tier1_frame_test_case(&test, &request, &output, &scratch, count < 33);
            }
        }
        tier1_frame_test_limits(&test, &output, &scratch);
        tier1_frame_test_guards(&test, &output, &scratch);
        tier1_frame_test_boundaries(&test, &output, &scratch);
    }
    arena_release(&scratch);
    arena_release(&output);
    fprintf(stderr, "tier1-frame: %" PRIu64 " checks, %" PRIu64 " failures (seed=%" PRIu64 ")\n", test.checks, test.failures, test.seed);
    return test.failures == 0 ? 0 : 1;
}
