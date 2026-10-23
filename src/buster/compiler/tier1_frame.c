// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 frame planning (#177), included after lane.c and calling_convention.c. The planner owns no IR: its borrowed
// size/alignment/lifetime columns come from the allocation and local-object producers. An optional compact index
// column lets #140's spill_values name those value columns directly. Lifetimes are half-open, nonempty intervals;
// address-taken objects must supply their conservative scope lifetime, including every possible escaped use.
//
// tier1_frame_requirements runs before allocation: reserve its register mask. tier1_frame_plan and its scalar twin
// return object-to-slot and object-to-byte-offset columns plus an explicit SysV prologue/epilogue plan. Nothing here
// emits instructions or unwind tables. The consumer must attach actual instruction PCs before #88 can use the steps.
//
// The vector path sorts ends before starts within (rounded size, alignment) classes, scans live pressure, finds each
// start's nearest earlier equal-or-greater pressure by pointer jumping, and inherits the slot of the intervening end.
// Roots are new pressure maxima; a second pointer-jump resolves inherited colors. Thus each class uses exactly its
// maximum live pressure. The scalar twin greedily takes the first free color in that same class; its colors may differ
// but its slot counts and frame sizes must agree. Preparation, sorting, color resolution and layout are all charged
// to the benchmark. There is no claim that a synthetic planner timing is a compiler or generated-program speedup.
//
// Input columns have column_count u32 elements, indices has count elements, and only those logical elements are read.
// Without indices the first count rows are used; with indices, every index must be distinct and in range. No padding
// or vector alignment is required. New output/scratch storage must not alias inputs or result; the arenas are disjoint.
// Input columns may live in a retained output-arena prefix. Result columns belong to the output arena.
// Empty inputs permit null columns. On failure the result and both arena lengths are unchanged. Scratch is restored
// on success too. Displacements and the worst-case stack extent must fit INT32_MAX; no arithmetic is truncated.
//
// Layout: API; bounded vector scans; stable radix sorting; preparation; the two colorers; frame layout and steps;
// transactional drivers. tier1_frame_prepare/color_*/layout are the same phases the benchmark measures separately.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_TIER1_FRAME_NONE UINT32_MAX
#define BUSTER_TIER1_FRAME_MAX_OBJECTS (INT32_MAX / 2u)
#define BUSTER_TIER1_FRAME_MAX_ALIGNMENT (1u << 30)
#define BUSTER_TIER1_FRAME_PAGE_BYTES 4096u
#define BUSTER_TIER1_FRAME_STEP_CAPACITY 32u
#define BUSTER_TIER1_FRAME_REALIGN_BASE BUSTER_ABI_R13

#define BUSTER_TIER1_FRAME_VARIADIC (1u << 0)
#define BUSTER_TIER1_FRAME_DYNAMIC_STACK (1u << 1)
#define BUSTER_TIER1_FRAME_KEEP_POINTER (1u << 2)
#define BUSTER_TIER1_FRAME_NO_RED_ZONE (1u << 3)
#define BUSTER_TIER1_FRAME_PROBE_STACK (1u << 4)
#define BUSTER_TIER1_FRAME_FLAGS ((1u << 5) - 1)

typedef enum Tier1FrameError
{
    TIER1_FRAME_OK,
    TIER1_FRAME_INVALID,
    TIER1_FRAME_CAPACITY,
    TIER1_FRAME_OVERFLOW,
    TIER1_FRAME_REGISTER_CONFLICT,
} Tier1FrameError;

typedef struct Tier1FrameSlots Tier1FrameSlots;
struct Tier1FrameSlots
{
    const uint32_t* start;
    const uint32_t* end;
    const uint32_t* size;
    const uint32_t* alignment;
    const uint32_t* indices;
    uint32_t count;
    uint32_t column_count;
};

typedef struct Tier1FrameRequest Tier1FrameRequest;
struct Tier1FrameRequest
{
    Tier1FrameSlots slots;
    const AbiCall* calls;
    uint32_t call_count;
    uint32_t used_callee_saved;
    uint32_t flags;
};

typedef struct Tier1FrameRequirements Tier1FrameRequirements;
struct Tier1FrameRequirements
{
    uint32_t alignment;
    uint32_t object_alignment;
    uint32_t outgoing_bytes;
    uint32_t reserved_registers;
    bool frame_pointer;
    bool realigned;
    bool dynamic_stack;
};

typedef enum Tier1FrameStepKind
{
    TIER1_FRAME_PUSH_POINTER,
    TIER1_FRAME_SET_POINTER,
    TIER1_FRAME_ALIGN_STACK,
    TIER1_FRAME_PROBE_ALIGN,
    TIER1_FRAME_ALLOCATE,
    TIER1_FRAME_PROBE_ALLOCATE,
    TIER1_FRAME_SAVE_REGISTER,
    TIER1_FRAME_SAVE_VARIADIC_INTEGER,
    // This step runs only if entry AL != 0, before any instruction destroys the entry AL value.
    TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL,
    TIER1_FRAME_SET_REALIGN_BASE,
    TIER1_FRAME_RESTORE_REGISTER,
    TIER1_FRAME_RELEASE,
    TIER1_FRAME_RESET_STACK,
    TIER1_FRAME_POP_POINTER,
} Tier1FrameStepKind;

typedef struct Tier1FrameStep Tier1FrameStep;
struct Tier1FrameStep
{
    Tier1FrameStepKind kind;
    uint32_t reg;
    uint32_t base;
    int32_t offset;
    uint32_t amount;
};

typedef struct Tier1Frame Tier1Frame;
struct Tier1Frame
{
    uint32_t count;
    uint32_t slot_count;
    uint32_t class_count;
    // Each input item i has a physical slot ID and byte displacement from base_register.
    uint32_t* slot;
    int32_t* offset;
    uint32_t base_register;
    uint32_t alignment;
    uint32_t object_bytes;
    uint32_t payload_bytes;
    uint32_t stack_adjust_bytes;
    uint32_t maximum_stack_bytes;
    uint32_t red_zone_bytes;
    uint32_t outgoing_bytes;
    // Dynamic frames reserve outgoing space at each call's current RSP, not in the fixed object area.
    bool dynamic_outgoing;
    bool frame_pointer;
    bool realigned;
    bool red_zone;
    bool variadic;
    int32_t variadic_offset;
    uint32_t saved_registers;
    int32_t save_offset[16];
    uint32_t prologue_count;
    uint32_t epilogue_count;
    Tier1FrameStep prologue[BUSTER_TIER1_FRAME_STEP_CAPACITY];
    Tier1FrameStep epilogue[BUSTER_TIER1_FRAME_STEP_CAPACITY];
};

// Scratch belongs to one prepared function. It is reusable for repeated coloring/layout and has no retained pointers
// into an output frame. All arrays have count elements except the event/sort/search arrays, which have 2 * count.
typedef struct Tier1FrameWork Tier1FrameWork;
struct Tier1FrameWork
{
    Tier1FrameRequirements requirements;
    uint32_t count;
    uint32_t class_count;
    uint64_t* key;
    uint64_t* temporary_key;
    uint32_t* event;
    uint32_t* temporary_event;
    uint32_t* pressure;
    uint32_t* nearest;
    uint32_t* next_nearest;
    uint32_t* object_class;
    uint32_t* parent;
    uint32_t* next_parent;
    uint32_t* root_color;
    uint32_t* color;
    uint32_t* class_stride;
    uint32_t* class_alignment;
    uint32_t* class_colors;
    uint32_t* class_slot;
    uint64_t* class_offset;
};

static __attribute__((unused)) Tier1FrameError tier1_frame_requirements(const Tier1FrameRequest* request, Tier1FrameRequirements* result);
static __attribute__((unused)) Tier1FrameError tier1_frame_plan(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result);
static __attribute__((unused)) Tier1FrameError tier1_frame_plan_scalar(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result);

static uint32_t tier1_frame_row(const Tier1FrameSlots* slots, uint32_t item)
{
    return slots->indices != 0 ? slots->indices[item] : item;
}

static uint64_t tier1_frame_align(uint64_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~((uint64_t)alignment - 1);
}

static __m512i tier1_frame_scan32(__m512i values)
{
    const __m512i lane = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_sub_epi32(lane, _mm512_set1_epi32(1)), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_sub_epi32(lane, _mm512_set1_epi32(2)), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_sub_epi32(lane, _mm512_set1_epi32(4)), values));
    values = _mm512_add_epi32(values, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_sub_epi32(lane, _mm512_set1_epi32(8)), values));
    return values;
}

static __m512i tier1_frame_scan64(__m512i values)
{
    const __m512i lane = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xfe, _mm512_sub_epi64(lane, _mm512_set1_epi64(1)), values));
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xfc, _mm512_sub_epi64(lane, _mm512_set1_epi64(2)), values));
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xf0, _mm512_sub_epi64(lane, _mm512_set1_epi64(4)), values));
    return values;
}

// Stable eight-bit radix passes. Event positions have the full u32 range: a dense counting table would make memory
// depend on the producer's position numbering. The ordered scatter is shared; its cost is included in preparation.
static void tier1_frame_sort(uint64_t* key, uint32_t* id, uint64_t* temporary_key, uint32_t* temporary_id, uint32_t count, uint32_t passes)
{
    for (uint32_t pass = 0; pass < passes; pass += 1)
    {
        uint32_t offsets[256] = {0};
        const uint32_t shift = pass * 8;
        for (uint32_t i = 0; i < count; i += 1)
        {
            offsets[(key[i] >> shift) & 255] += 1;
        }
        uint32_t sum = 0;
        for (uint32_t bucket = 0; bucket < 256; bucket += 1)
        {
            const uint32_t amount = offsets[bucket];
            offsets[bucket] = sum;
            sum += amount;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t bucket = (key[i] >> shift) & 255;
            const uint32_t at = offsets[bucket];
            offsets[bucket] += 1;
            temporary_key[at] = key[i];
            temporary_id[at] = id[i];
        }
        uint64_t* swap_key = key;
        key = temporary_key;
        temporary_key = swap_key;
        uint32_t* swap_id = id;
        id = temporary_id;
        temporary_id = swap_id;
    }
    // Every caller uses an even number of passes, so the sorted columns are back in the caller's original arrays.
}

static Tier1FrameError tier1_frame_header(const Tier1FrameRequest* request)
{
    const Tier1FrameSlots* slots = &request->slots;
    Tier1FrameError error = TIER1_FRAME_OK;
    if ((request->flags & ~BUSTER_TIER1_FRAME_FLAGS) != 0 || (request->used_callee_saved & ~BUSTER_ABI_CALLEE_SAVED) != 0 ||
        slots->count > BUSTER_TIER1_FRAME_MAX_OBJECTS || (slots->indices == 0 && slots->count > slots->column_count) ||
        (slots->count != 0 && (slots->start == 0 || slots->end == 0 || slots->size == 0 || slots->alignment == 0)) ||
        (request->call_count != 0 && request->calls == 0))
    {
        error = TIER1_FRAME_INVALID;
    }
    for (uint32_t i = 0; error == TIER1_FRAME_OK && slots->indices != 0 && i < slots->count; i += 1)
    {
        if (slots->indices[i] >= slots->column_count)
        {
            error = TIER1_FRAME_INVALID;
        }
    }
    return error;
}

// Validation and class-key production share the scan. Eight rows are converted to qwords so rounding cannot wrap.
// Indexed inputs use scalar loads into a bounded staging block instead of Zen 5's expensive gathers.
static Tier1FrameError tier1_frame_rows(const Tier1FrameSlots* slots, bool scalar, uint64_t* keys, uint32_t* maximum_alignment)
{
    Tier1FrameError error = TIER1_FRAME_OK;
    uint32_t largest = 1;
    if (scalar)
    {
        for (uint32_t i = 0; error == TIER1_FRAME_OK && i < slots->count; i += 1)
        {
            const uint32_t row = tier1_frame_row(slots, i);
            const uint32_t alignment = slots->alignment[row];
            if (slots->start[row] >= slots->end[row] || slots->size[row] == 0 || alignment == 0 ||
                (alignment & (alignment - 1)) != 0 || alignment > BUSTER_TIER1_FRAME_MAX_ALIGNMENT)
            {
                error = TIER1_FRAME_INVALID;
            }
            else
            {
                const uint64_t stride = tier1_frame_align(slots->size[row], alignment);
                if (stride > INT32_MAX)
                {
                    error = TIER1_FRAME_OVERFLOW;
                }
                else
                {
                    largest = largest > alignment ? largest : alignment;
                    if (keys != 0)
                    {
                        keys[i] = (uint64_t)(UINT32_MAX - alignment) << 32 | stride;
                    }
                }
            }
        }
    }
    else
    {
        for (uint32_t i = 0; error == TIER1_FRAME_OK && i < slots->count; i += 8)
        {
            const uint32_t live = slots->count - i < 8 ? slots->count - i : 8;
            const __mmask8 mask = (__mmask8)((1u << live) - 1);
            uint32_t staged[4][8];
            const uint32_t* starts;
            const uint32_t* ends;
            const uint32_t* sizes;
            const uint32_t* alignments;
            if (slots->indices != 0)
            {
                for (uint32_t lane = 0; lane < live; lane += 1)
                {
                    const uint32_t row = slots->indices[i + lane];
                    staged[0][lane] = slots->start[row];
                    staged[1][lane] = slots->end[row];
                    staged[2][lane] = slots->size[row];
                    staged[3][lane] = slots->alignment[row];
                }
                starts = staged[0];
                ends = staged[1];
                sizes = staged[2];
                alignments = staged[3];
            }
            else
            {
                starts = slots->start + i;
                ends = slots->end + i;
                sizes = slots->size + i;
                alignments = slots->alignment + i;
            }
            const __m512i start = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, starts));
            const __m512i end = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, ends));
            const __m512i size = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, sizes));
            const __m512i alignment = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, alignments));
            const __m512i minus_one = _mm512_sub_epi64(alignment, _mm512_set1_epi64(1));
            const __m512i stride = _mm512_andnot_si512(minus_one, _mm512_add_epi64(size, minus_one));
            __mmask8 valid = _mm512_cmplt_epu64_mask(start, end);
            valid &= _mm512_cmpneq_epi64_mask(size, _mm512_setzero_si512());
            valid &= _mm512_cmpneq_epi64_mask(alignment, _mm512_setzero_si512());
            valid &= _mm512_cmpeq_epi64_mask(_mm512_and_si512(alignment, minus_one), _mm512_setzero_si512());
            valid &= _mm512_cmple_epu64_mask(alignment, _mm512_set1_epi64(BUSTER_TIER1_FRAME_MAX_ALIGNMENT));
            if ((valid & mask) != mask)
            {
                error = TIER1_FRAME_INVALID;
            }
            else if ((_mm512_cmpgt_epu64_mask(stride, _mm512_set1_epi64(INT32_MAX)) & mask) != 0)
            {
                error = TIER1_FRAME_OVERFLOW;
            }
            else
            {
                const uint32_t current = (uint32_t)_mm512_reduce_max_epu64(alignment);
                largest = largest > current ? largest : current;
                if (keys != 0)
                {
                    const __m512i high = _mm512_slli_epi64(_mm512_sub_epi64(_mm512_set1_epi64(UINT32_MAX), alignment), 32);
                    _mm512_mask_storeu_epi64(keys + i, mask, _mm512_or_si512(high, stride));
                }
            }
        }
    }
    if (error == TIER1_FRAME_OK)
    {
        *maximum_alignment = largest;
    }
    return error;
}

static Tier1FrameError tier1_frame_call_requirements(const Tier1FrameRequest* request, uint32_t object_alignment, Tier1FrameRequirements* result)
{
    Tier1FrameRequirements candidate = {
        .alignment = object_alignment > 16 ? object_alignment : 16,
        .object_alignment = object_alignment,
        .dynamic_stack = (request->flags & BUSTER_TIER1_FRAME_DYNAMIC_STACK) != 0,
    };
    Tier1FrameError error = TIER1_FRAME_OK;
    for (uint32_t i = 0; error == TIER1_FRAME_OK && i < request->call_count; i += 1)
    {
        const AbiCall call = request->calls[i];
        if (call.stack_alignment_log2 < BUSTER_ABI_STACK_ALIGNMENT_LOG2 || call.stack_alignment_log2 > 30 ||
            (call.stack_bytes & (BUSTER_ABI_SLOT_BYTES - 1)) != 0 || call.integer_registers > BUSTER_ABI_INTEGER_ARGUMENTS ||
            call.vector_registers > BUSTER_ABI_VECTOR_ARGUMENTS)
        {
            error = TIER1_FRAME_INVALID;
        }
        else if (call.stack_bytes > INT32_MAX)
        {
            error = TIER1_FRAME_OVERFLOW;
        }
        else
        {
            const uint32_t alignment = 1u << call.stack_alignment_log2;
            candidate.alignment = candidate.alignment > alignment ? candidate.alignment : alignment;
            candidate.outgoing_bytes = candidate.outgoing_bytes > call.stack_bytes ? candidate.outgoing_bytes : call.stack_bytes;
        }
    }
    candidate.realigned = candidate.alignment > 16;
    candidate.frame_pointer = candidate.realigned || candidate.dynamic_stack || (request->flags & BUSTER_TIER1_FRAME_KEEP_POINTER) != 0;
    candidate.reserved_registers = candidate.frame_pointer ? 1u << BUSTER_ABI_RBP : 0;
    if (candidate.realigned && candidate.dynamic_stack)
    {
        candidate.reserved_registers |= 1u << BUSTER_TIER1_FRAME_REALIGN_BASE;
    }
    if (error == TIER1_FRAME_OK)
    {
        *result = candidate;
    }
    return error;
}

// This pre-allocation query checks rows and ABI totals but does not allocate or check indexed identity uniqueness;
// prepare checks that before producing any frame. It ignores used_callee_saved when choosing the reserved mask.
static Tier1FrameError tier1_frame_requirements(const Tier1FrameRequest* request, Tier1FrameRequirements* result)
{
    Tier1FrameError error = tier1_frame_header(request);
    uint32_t alignment = 1;
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_rows(&request->slots, false, 0, &alignment);
    }
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_call_requirements(request, alignment, result);
    }
    return error;
}

static Tier1FrameError tier1_frame_prepare(const Tier1FrameRequest* request, Arena* scratch, bool scalar, Tier1FrameWork* result)
{
    const uint64_t mark = scratch->length;
    Tier1FrameError error = tier1_frame_header(request);
    Tier1FrameWork candidate = {
        .count = request->slots.count,
    };
    const uint32_t count = candidate.count;
    const uint64_t events = (uint64_t)count * 2;
    if (error == TIER1_FRAME_OK && count != 0)
    {
        candidate.key = arena_push_array(scratch, events, 8, 8);
        candidate.temporary_key = arena_push_array(scratch, events, 8, 8);
        candidate.event = arena_push_array(scratch, events, 4, 4);
        candidate.temporary_event = arena_push_array(scratch, events, 4, 4);
        candidate.pressure = arena_push_array(scratch, events, 4, 4);
        candidate.nearest = arena_push_array(scratch, events, 4, 4);
        candidate.next_nearest = arena_push_array(scratch, events, 4, 4);
        candidate.object_class = arena_push_array(scratch, count, 4, 4);
        candidate.parent = arena_push_array(scratch, count, 4, 4);
        candidate.next_parent = arena_push_array(scratch, count, 4, 4);
        candidate.root_color = arena_push_array(scratch, count, 4, 4);
        candidate.color = arena_push_array(scratch, count, 4, 4);
        candidate.class_stride = arena_push_array(scratch, count, 4, 4);
        candidate.class_alignment = arena_push_array(scratch, count, 4, 4);
        candidate.class_colors = arena_push_array(scratch, count, 4, 4);
        candidate.class_slot = arena_push_array(scratch, count, 4, 4);
        candidate.class_offset = arena_push_array(scratch, count, 8, 8);
        if (candidate.key == 0 || candidate.temporary_key == 0 || candidate.event == 0 || candidate.temporary_event == 0 ||
            candidate.pressure == 0 || candidate.nearest == 0 || candidate.next_nearest == 0 || candidate.object_class == 0 ||
            candidate.parent == 0 || candidate.next_parent == 0 || candidate.root_color == 0 || candidate.color == 0 ||
            candidate.class_stride == 0 || candidate.class_alignment == 0 || candidate.class_colors == 0 ||
            candidate.class_slot == 0 || candidate.class_offset == 0)
        {
            error = TIER1_FRAME_CAPACITY;
        }
    }
    if (error == TIER1_FRAME_OK && request->slots.indices != 0 && count != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            candidate.key[i] = request->slots.indices[i];
            candidate.event[i] = i;
        }
        tier1_frame_sort(candidate.key, candidate.event, candidate.temporary_key, candidate.temporary_event, count, 4);
        for (uint32_t i = 1; error == TIER1_FRAME_OK && i < count; i += 1)
        {
            if (candidate.key[i] == candidate.key[i - 1])
            {
                error = TIER1_FRAME_INVALID;
            }
        }
    }
    uint32_t alignment = 1;
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_rows(&request->slots, scalar, candidate.key, &alignment);
    }
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_call_requirements(request, alignment, &candidate.requirements);
    }
    if (error == TIER1_FRAME_OK && (request->used_callee_saved & candidate.requirements.reserved_registers) != 0)
    {
        error = TIER1_FRAME_REGISTER_CONFLICT;
    }
    if (error == TIER1_FRAME_OK && count != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            candidate.event[i] = i;
        }
        tier1_frame_sort(candidate.key, candidate.event, candidate.temporary_key, candidate.temporary_event, count, 8);
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (i == 0 || candidate.key[i] != candidate.key[i - 1])
            {
                const uint32_t group = candidate.class_count;
                candidate.class_stride[group] = (uint32_t)candidate.key[i];
                candidate.class_alignment[group] = UINT32_MAX - (uint32_t)(candidate.key[i] >> 32);
                candidate.class_count += 1;
            }
            candidate.object_class[candidate.event[i]] = candidate.class_count - 1;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t row = tier1_frame_row(&request->slots, i);
            const uint64_t group = (uint64_t)candidate.object_class[i] << 32;
            candidate.key[i] = group | request->slots.end[row];
            candidate.event[i] = i * 2;
            candidate.key[count + i] = group | request->slots.start[row];
            candidate.event[count + i] = i * 2 + 1;
        }
        // The stable sort keeps all ends before starts at an equal position, allowing adjacent lifetimes to share.
        tier1_frame_sort(candidate.key, candidate.event, candidate.temporary_key, candidate.temporary_event, count * 2, 8);
    }
    if (error == TIER1_FRAME_OK)
    {
        *result = candidate;
    }
    else
    {
        arena_pop_to(scratch, mark);
    }
    return error;
}

static void tier1_frame_color_scalar(const Tier1FrameRequest* request, Tier1FrameWork* work)
{
    if (work->class_count != 0)
    {
        memset(work->class_colors, 0, (uint64_t)work->class_count * 4);
    }
    for (uint32_t i = 0; i < work->count * 2; i += 1)
    {
        if ((work->event[i] & 1) != 0)
        {
            const uint32_t item = work->event[i] >> 1;
            const uint32_t row = tier1_frame_row(&request->slots, item);
            const uint32_t group = work->object_class[item];
            const uint32_t colors = work->class_colors[group];
            uint32_t color = 0;
            while (color < colors && work->nearest[color] > request->slots.start[row])
            {
                color += 1;
            }
            work->nearest[color] = request->slots.end[row];
            work->color[item] = color;
            work->class_colors[group] = color == colors ? colors + 1 : colors;
        }
    }
}

static void tier1_frame_color_vector(Tier1FrameWork* work)
{
    const uint32_t events = work->count * 2;
    uint32_t carry = 0;
    for (uint32_t i = 0; i < events; i += 16)
    {
        const uint32_t live = events - i < 16 ? events - i : 16;
        const __mmask16 mask = (__mmask16)((1u << live) - 1);
        const __m512i ids = _mm512_maskz_loadu_epi32(mask, work->event + i);
        const __m512i is_start = _mm512_and_si512(ids, _mm512_set1_epi32(1));
        const __m512i delta = _mm512_maskz_mov_epi32(mask, _mm512_sub_epi32(_mm512_slli_epi32(is_start, 1), _mm512_set1_epi32(1)));
        const __m512i pressure = _mm512_add_epi32(tier1_frame_scan32(delta), _mm512_set1_epi32(carry));
        _mm512_mask_storeu_epi32(work->pressure + i, mask, pressure);
        carry += (uint32_t)_mm512_reduce_add_epi32(delta);
    }
    for (uint32_t i = 0; i < events; i += 1)
    {
        work->nearest[i] = i == 0 || (work->key[i] >> 32) != (work->key[i - 1] >> 32) ? BUSTER_TIER1_FRAME_NONE : i - 1;
    }
    bool changed = events != 0;
    uint32_t* nearest = work->nearest;
    uint32_t* next = work->next_nearest;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < events; i += 16)
        {
            const uint32_t live = events - i < 16 ? events - i : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            uint32_t previous_pressure[16];
            uint32_t predecessor[16];
            for (uint32_t lane = 0; lane < live; lane += 1)
            {
                const uint32_t previous = nearest[i + lane];
                previous_pressure[lane] = previous == BUSTER_TIER1_FRAME_NONE ? UINT32_MAX : work->pressure[previous];
                predecessor[lane] = previous == BUSTER_TIER1_FRAME_NONE ? BUSTER_TIER1_FRAME_NONE : nearest[previous];
            }
            const __m512i previous = _mm512_maskz_loadu_epi32(mask, previous_pressure);
            const __m512i pressure = _mm512_maskz_loadu_epi32(mask, work->pressure + i);
            const __mmask16 skip = _mm512_cmplt_epu32_mask(previous, pressure) & mask;
            const __m512i parents = _mm512_maskz_loadu_epi32(mask, nearest + i);
            const __m512i grandparents = _mm512_maskz_loadu_epi32(mask, predecessor);
            _mm512_mask_storeu_epi32(next + i, mask, _mm512_mask_mov_epi32(parents, skip, grandparents));
            changed |= skip != 0;
        }
        uint32_t* swap = nearest;
        nearest = next;
        next = swap;
    }
    if (work->class_count != 0)
    {
        memset(work->class_colors, 0, (uint64_t)work->class_count * 4);
    }
    for (uint32_t i = 0; i < events; i += 1)
    {
        if ((work->event[i] & 1) != 0)
        {
            const uint32_t item = work->event[i] >> 1;
            const uint32_t previous = nearest[i];
            work->parent[item] = previous == BUSTER_TIER1_FRAME_NONE ? item : work->event[previous + 1] >> 1;
            work->root_color[item] = work->pressure[i] - 1;
            if (previous == BUSTER_TIER1_FRAME_NONE)
            {
                work->class_colors[work->object_class[item]] = work->pressure[i];
            }
        }
    }
    uint32_t* parent = work->parent;
    uint32_t* next_parent = work->next_parent;
    changed = work->count != 0;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < work->count; i += 16)
        {
            const uint32_t live = work->count - i < 16 ? work->count - i : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            uint32_t grandparents[16];
            for (uint32_t lane = 0; lane < live; lane += 1)
            {
                grandparents[lane] = parent[parent[i + lane]];
            }
            const __m512i before = _mm512_maskz_loadu_epi32(mask, parent + i);
            const __m512i after = _mm512_maskz_loadu_epi32(mask, grandparents);
            _mm512_mask_storeu_epi32(next_parent + i, mask, after);
            changed |= (_mm512_cmpneq_epi32_mask(before, after) & mask) != 0;
        }
        uint32_t* swap = parent;
        parent = next_parent;
        next_parent = swap;
    }
    for (uint32_t i = 0; i < work->count; i += 1)
    {
        work->color[i] = work->root_color[parent[i]];
    }
}

static void tier1_frame_step(Tier1FrameStep* steps, uint32_t* count, Tier1FrameStepKind kind, uint32_t reg, uint32_t base, int32_t offset, uint32_t amount)
{
    // At most 25 prologue and 9 epilogue steps: one pointer push/set, one align, one allocation, six GPR saves,
    // fourteen variadic saves, and one stable-base set. These are per-function fixed bounds, not input-sized lists.
    assert(*count < BUSTER_TIER1_FRAME_STEP_CAPACITY);
    steps[*count] = (Tier1FrameStep){
        .kind = kind,
        .reg = reg,
        .base = base,
        .offset = offset,
        .amount = amount,
    };
    *count += 1;
}

static void tier1_frame_steps(const Tier1FrameRequest* request, const Tier1FrameRequirements* requirements, Tier1Frame* frame)
{
    if (frame->frame_pointer)
    {
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_PUSH_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, -8, 8);
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SET_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, 0, 0);
    }
    if (frame->realigned)
    {
        const Tier1FrameStepKind kind = request->flags & BUSTER_TIER1_FRAME_PROBE_STACK ? TIER1_FRAME_PROBE_ALIGN : TIER1_FRAME_ALIGN_STACK;
        tier1_frame_step(frame->prologue, &frame->prologue_count, kind, BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->alignment);
    }
    if (frame->stack_adjust_bytes != 0)
    {
        const bool probe = (request->flags & BUSTER_TIER1_FRAME_PROBE_STACK) != 0 && frame->stack_adjust_bytes >= BUSTER_TIER1_FRAME_PAGE_BYTES;
        tier1_frame_step(frame->prologue, &frame->prologue_count, probe ? TIER1_FRAME_PROBE_ALLOCATE : TIER1_FRAME_ALLOCATE,
            BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->stack_adjust_bytes);
    }
    // R13's original value must be stored before it becomes the realigned base. Every prologue store addresses RSP;
    // a non-realigned frame's RBP-relative displacements differ from RSP-relative ones by the fixed subtraction.
    const int64_t to_rsp = frame->base_register == BUSTER_ABI_RBP ? frame->stack_adjust_bytes : 0;
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        if ((frame->saved_registers & (1u << reg)) != 0)
        {
            tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SAVE_REGISTER, reg, BUSTER_ABI_RSP,
                (int32_t)(frame->save_offset[reg] + to_rsp), 8);
        }
    }
    if (frame->variadic)
    {
        for (uint32_t i = 0; i < BUSTER_ABI_INTEGER_ARGUMENTS; i += 1)
        {
            tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SAVE_VARIADIC_INTEGER, abi_integer_arguments[i], BUSTER_ABI_RSP,
                (int32_t)(frame->variadic_offset + to_rsp + i * 8), 8);
        }
        for (uint32_t i = 0; i < BUSTER_ABI_VECTOR_ARGUMENTS; i += 1)
        {
            tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL,
                BUSTER_ABI_REGISTER_VECTOR + i, BUSTER_ABI_RSP,
                (int32_t)(frame->variadic_offset + to_rsp + BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + i * BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES),
                BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES);
        }
    }
    if (frame->base_register == BUSTER_TIER1_FRAME_REALIGN_BASE && requirements->dynamic_stack && frame->realigned)
    {
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SET_REALIGN_BASE, BUSTER_TIER1_FRAME_REALIGN_BASE, BUSTER_ABI_RSP, 0, 0);
    }
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        if ((frame->saved_registers & (1u << reg)) != 0 && reg != frame->base_register)
        {
            tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RESTORE_REGISTER, reg, frame->base_register, frame->save_offset[reg], 8);
        }
    }
    if ((frame->saved_registers & (1u << frame->base_register)) != 0)
    {
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RESTORE_REGISTER, frame->base_register, frame->base_register,
            frame->save_offset[frame->base_register], 8);
    }
    if (frame->frame_pointer)
    {
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RESET_STACK, BUSTER_ABI_RSP, BUSTER_ABI_RBP, 0, 0);
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_POP_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, 0, 8);
    }
    else if (frame->stack_adjust_bytes != 0)
    {
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RELEASE, BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->stack_adjust_bytes);
    }
}

static Tier1FrameError tier1_frame_layout(const Tier1FrameRequest* request, Tier1FrameWork* work, Arena* output, bool scalar, Tier1Frame* result)
{
    const uint64_t mark = output->length;
    const Tier1FrameRequirements requirements = work->requirements;
    Tier1Frame candidate = {
        .count = work->count,
        .class_count = work->class_count,
        .alignment = requirements.alignment,
        .outgoing_bytes = requirements.outgoing_bytes,
        .dynamic_outgoing = requirements.dynamic_stack && request->call_count != 0,
        .frame_pointer = requirements.frame_pointer,
        .realigned = requirements.realigned,
        .variadic = (request->flags & BUSTER_TIER1_FRAME_VARIADIC) != 0,
        .saved_registers = request->used_callee_saved,
    };
    if (requirements.realigned && requirements.dynamic_stack)
    {
        candidate.saved_registers |= 1u << BUSTER_TIER1_FRAME_REALIGN_BASE;
    }
    const uint64_t outgoing = requirements.dynamic_stack ? 0 : requirements.outgoing_bytes;
    const uint64_t object_begin = work->count == 0 ? outgoing : tier1_frame_align(outgoing, requirements.object_alignment);
    uint64_t object_bytes = 0;
    uint32_t slots = 0;
    if (scalar)
    {
        for (uint32_t group = 0; group < work->class_count; group += 1)
        {
            work->class_offset[group] = object_begin + object_bytes;
            work->class_slot[group] = slots;
            object_bytes += (uint64_t)work->class_stride[group] * work->class_colors[group];
            slots += work->class_colors[group];
        }
    }
    else
    {
        for (uint32_t group = 0; group < work->class_count; group += 8)
        {
            const uint32_t live = work->class_count - group < 8 ? work->class_count - group : 8;
            const __mmask8 mask = (__mmask8)((1u << live) - 1);
            const __m512i size = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, work->class_stride + group));
            const __m512i colors = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(mask, work->class_colors + group));
            const __m512i bytes = _mm512_mullo_epi64(size, colors);
            const __m512i ends = tier1_frame_scan64(bytes);
            const __m512i begins = _mm512_add_epi64(_mm512_sub_epi64(ends, bytes), _mm512_set1_epi64(object_begin + object_bytes));
            _mm512_mask_storeu_epi64(work->class_offset + group, mask, begins);
            const __m512i slot_ends = tier1_frame_scan64(colors);
            const __m512i slot_begins = _mm512_add_epi64(_mm512_sub_epi64(slot_ends, colors), _mm512_set1_epi64(slots));
            _mm256_mask_storeu_epi32(work->class_slot + group, mask, _mm512_cvtepi64_epi32(slot_begins));
            object_bytes += (uint64_t)_mm512_reduce_add_epi64(bytes);
            slots += (uint32_t)_mm512_reduce_add_epi64(colors);
        }
    }
    uint64_t position = object_begin + object_bytes;
    const uint64_t variadic_begin = candidate.variadic ? tier1_frame_align(position, 16) : 0;
    if (candidate.variadic)
    {
        position = variadic_begin + BUSTER_ABI_SAVE_AREA_BYTES;
    }
    const uint64_t saves_begin = candidate.saved_registers != 0 ? tier1_frame_align(position, 8) : position;
    position = saves_begin + (uint32_t)__builtin_popcount(candidate.saved_registers) * 8;
    uint32_t data_alignment = requirements.object_alignment;
    data_alignment = candidate.variadic && data_alignment < 16 ? 16 : data_alignment;
    data_alignment = candidate.saved_registers != 0 && data_alignment < 8 ? 8 : data_alignment;
    // Entry RSP is 8 modulo 16. A red-zone base must compensate for that residue, including sixteen-byte objects.
    const uint32_t residue = data_alignment <= 16 ? 8 % data_alignment : 0;
    const uint64_t red_span = tier1_frame_align(position + residue, data_alignment) - residue;
    candidate.red_zone = request->call_count == 0 && !candidate.frame_pointer && !requirements.dynamic_stack &&
        (request->flags & BUSTER_TIER1_FRAME_NO_RED_ZONE) == 0 && red_span <= BUSTER_ABI_RED_ZONE_BYTES;
    uint64_t adjustment = 0;
    if (!candidate.red_zone && (position != 0 || request->call_count != 0))
    {
        const uint32_t entry_residue = candidate.frame_pointer ? 0 : 8;
        adjustment = tier1_frame_align(position + entry_residue, requirements.alignment) - entry_residue;
    }
    const uint64_t maximum = candidate.red_zone ? red_span : adjustment + (candidate.frame_pointer ? 8 : 0) + (candidate.realigned ? requirements.alignment - 16 : 0);
    Tier1FrameError error = position <= INT32_MAX && adjustment <= INT32_MAX && maximum <= INT32_MAX ? TIER1_FRAME_OK : TIER1_FRAME_OVERFLOW;
    if (error == TIER1_FRAME_OK && work->count != 0)
    {
        candidate.slot = arena_push_array(output, work->count, 4, 4);
        candidate.offset = arena_push_array(output, work->count, 4, 4);
        if (candidate.slot == 0 || candidate.offset == 0)
        {
            error = TIER1_FRAME_CAPACITY;
        }
    }
    if (error == TIER1_FRAME_OK)
    {
        candidate.slot_count = slots;
        candidate.object_bytes = (uint32_t)object_bytes;
        candidate.payload_bytes = (uint32_t)position;
        candidate.stack_adjust_bytes = (uint32_t)adjustment;
        candidate.maximum_stack_bytes = (uint32_t)maximum;
        candidate.red_zone_bytes = candidate.red_zone ? (uint32_t)red_span : 0;
        candidate.base_register = requirements.dynamic_stack ? (candidate.realigned ? BUSTER_TIER1_FRAME_REALIGN_BASE : BUSTER_ABI_RBP) : BUSTER_ABI_RSP;
        const int64_t bias = candidate.red_zone ? -(int64_t)red_span : (candidate.base_register == BUSTER_ABI_RBP ? -(int64_t)adjustment : 0);
        for (uint32_t i = 0; i < work->count; i += 1)
        {
            const uint32_t group = work->object_class[i];
            candidate.slot[i] = work->class_slot[group] + work->color[i];
            candidate.offset[i] = (int32_t)((int64_t)(work->class_offset[group] + (uint64_t)work->color[i] * work->class_stride[group]) + bias);
        }
        candidate.variadic_offset = candidate.variadic ? (int32_t)((int64_t)variadic_begin + bias) : 0;
        uint32_t saved_index = 0;
        for (uint32_t reg = 0; reg < 16; reg += 1)
        {
            if ((candidate.saved_registers & (1u << reg)) != 0)
            {
                candidate.save_offset[reg] = (int32_t)((int64_t)(saves_begin + saved_index * 8) + bias);
                saved_index += 1;
            }
        }
        tier1_frame_steps(request, &requirements, &candidate);
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static Tier1FrameError tier1_frame_plan_path(const Tier1FrameRequest* request, Arena* output, Arena* scratch, bool scalar, Tier1Frame* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1FrameError error = output == scratch ? TIER1_FRAME_INVALID : TIER1_FRAME_OK;
    Tier1FrameWork work;
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_prepare(request, scratch, scalar, &work);
    }
    if (error == TIER1_FRAME_OK)
    {
        if (scalar)
        {
            tier1_frame_color_scalar(request, &work);
        }
        else
        {
            tier1_frame_color_vector(&work);
        }
        error = tier1_frame_layout(request, &work, output, scalar, result);
    }
    arena_pop_to(scratch, scratch_mark);
    if (error != TIER1_FRAME_OK)
    {
        arena_pop_to(output, output_mark);
    }
    return error;
}

static Tier1FrameError tier1_frame_plan(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result)
{
    return tier1_frame_plan_path(request, output, scratch, false, result);
}

static Tier1FrameError tier1_frame_plan_scalar(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result)
{
    return tier1_frame_plan_path(request, output, scratch, true, result);
}
