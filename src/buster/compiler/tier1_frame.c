// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 frame planning (#177), included after lane.c and calling_convention.c. The planner owns no IR: its borrowed
// size/alignment/lifetime columns come from the allocation and local-object producers. An optional compact index
// column lets #140's spill_values name those value columns directly. Lifetimes are half-open, nonempty intervals;
// address-taken objects must supply their conservative scope lifetime, including every possible escaped use.
//
// tier1_frame_requirements runs before allocation: reserve its register mask. tier1_frame_plan returns object-to-slot and object-to-byte-offset columns plus an explicit SysV prologue/epilogue plan. Nothing here
// emits instructions or unwind tables. The plan is Tier 0's frame protocol, which unwind_tier0_frame_tails and unwind.c
// describe (#88, #177): rbp pushed and pointed at its push when the frame keeps a pointer, then the callee-saved
// registers pushed in RBX, RBP, R12, R13, R14, R15 order, then one subtraction, then a realignment's `and` and its
// base's copy (R15, one of the pushes). The epilogue releases the subtraction (or resets rsp from rbp, past the pushes)
// and pops in reverse. The consumer attaches actual instruction PCs to those transitions.
//
// Preparation sorts every item's end and start by (rounded size, alignment) class and position, ends before starts at
// an equal position. A function's frames are small (half of sqlite3.c's 1,368 have two objects or fewer, 98% at most
// 32, and all but one at most eight classes; #2699), so preparation classes items against a register of up to eight
// distinct class keys (tier1_frame_classes) and sorts up to BUSTER_TIER1_FRAME_RANK_EVENTS events by rank, every key
// compared with eight others at a time (tier1_frame_rank). Larger inputs (a ninth class, more events) sort by stable
// radix passes over the key bytes that differ (tier1_frame_sort); both sorts give one order. One ordered, branch-free
// pass then colors (tier1_frame_color): a class's free colors are a stack, so each class uses exactly its maximum live
// pressure. The scalar twin went in #2924, after the vector path read 0.33 to 0.92 of its time at every frame size of
// sqlite3.c's, Lua's and musl's real frames on the 9700X. The tests check each plan independently, preparation's order
// by its definition, the colors against pointer jumping's, and the default run's plans against a frozen digest.
// Preparation, sorting, coloring and layout are all charged to the benchmark. There is no claim that a synthetic
// planner timing is a compiler or generated-program speedup.
//
// Input columns have column_count u32 elements, indices has count elements, and only those logical elements are read.
// Without indices the first count rows are used; with indices, every index must be distinct and in range. No padding
// or vector alignment is required. New output/scratch storage must not alias inputs or result; the arenas are disjoint.
// Input columns may live in a retained output-arena prefix. Result columns belong to the output arena.
// Empty inputs permit null columns. On failure the result and both arena lengths are unchanged. Scratch is restored
// on success too. Displacements and the worst-case stack extent must fit INT32_MAX; no arithmetic is truncated.
//
// Layout: API; bounded vector scans; the sorts and the class register; preparation; the coloring pass; frame layout
// and steps; transactional drivers. tier1_frame_prepare/color/layout are the same phases the benchmark measures
// separately.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_FRAME_NONE UINT32_MAX
#define BUSTER_TIER1_FRAME_MAX_OBJECTS (INT32_MAX / 2u)
#define BUSTER_TIER1_FRAME_MAX_ALIGNMENT (1u << 30)
#define BUSTER_TIER1_FRAME_PAGE_BYTES 4096u
#define BUSTER_TIER1_FRAME_STEP_CAPACITY 32u
#define BUSTER_TIER1_FRAME_REALIGN_BASE BUSTER_ABI_R15
// The most events preparation sorts by rank (tier1_frame_rank), a multiple of 8 and at most 64.
#define BUSTER_TIER1_FRAME_RANK_EVENTS 64u

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

// A push or pop moves rsp by 8 and writes or reads the word at the new or old rsp (`base` RSP, `offset` -8 or 0).
// RESET_STACK sets rsp to `base` + `offset` (rbp less the pushes below it).
typedef enum Tier1FrameStepKind
{
    TIER1_FRAME_PUSH_POINTER,
    TIER1_FRAME_SET_POINTER,
    TIER1_FRAME_ALIGN_STACK,
    TIER1_FRAME_PROBE_ALIGN,
    TIER1_FRAME_ALLOCATE,
    TIER1_FRAME_PROBE_ALLOCATE,
    TIER1_FRAME_PUSH_REGISTER,
    TIER1_FRAME_SAVE_VARIADIC_INTEGER,
    // This step runs only if entry AL != 0, before any instruction destroys the entry AL value.
    TIER1_FRAME_SAVE_VARIADIC_VECTOR_IF_AL,
    TIER1_FRAME_SET_REALIGN_BASE,
    TIER1_FRAME_POP_REGISTER,
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
    // BUSTER_TIER1_FRAME_PROBE_STACK's (#177, -fstack-clash-protection): the prologue probes its allocation (the steps'
    // PROBE_ALLOCATE and PROBE_ALIGN), and the instruction consumer the body's dynamic allocations
    // (tier1_emit_cfg_probed_allocation).
    bool probe_stack;
    int32_t variadic_offset;
    // The callee-saved registers the prologue pushes and each one's slot: from rbp with a frame pointer, otherwise from
    // the post-prologue rsp. RBP's own push with a frame pointer is its prologue/epilogue steps', not one of these.
    uint32_t saved_registers;
    int32_t save_offset[16];
    uint32_t prologue_count;
    uint32_t epilogue_count;
    Tier1FrameStep prologue[BUSTER_TIER1_FRAME_STEP_CAPACITY];
    Tier1FrameStep epilogue[BUSTER_TIER1_FRAME_STEP_CAPACITY];
};

// Scratch belongs to one prepared function. It is reusable for repeated coloring/layout and has no retained pointers
// into an output frame. `event` has 2 * count elements, the sorted ends (item * 2) and starts (item * 2 + 1); `last`,
// a pressure level's last ended item, and `color` have count + 1, each with a spare element the coloring pass stores to
// (tier1_frame_color); every other array has count.
typedef struct Tier1FrameWork Tier1FrameWork;
struct Tier1FrameWork
{
    Tier1FrameRequirements requirements;
    uint32_t count;
    uint32_t class_count;
    uint32_t* event;
    uint32_t* last;
    uint32_t* color;
    uint32_t* object_class;
    uint32_t* class_stride;
    uint32_t* class_alignment;
    uint32_t* class_colors;
    uint32_t* class_slot;
    uint64_t* class_offset;
};

static __attribute__((unused)) Tier1FrameError tier1_frame_requirements(const Tier1FrameRequest* request, Tier1FrameRequirements* result);
static __attribute__((unused)) Tier1FrameError tier1_frame_plan(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result);

static uint32_t tier1_frame_row(const Tier1FrameSlots* slots, uint32_t item)
{
    return slots->indices != 0 ? slots->indices[item] : item;
}

static uint64_t tier1_frame_align(uint64_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~((uint64_t)alignment - 1);
}

static __m512i tier1_frame_scan64(__m512i values)
{
    const __m512i lane = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xfe, _mm512_sub_epi64(lane, _mm512_set1_epi64(1)), values));
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xfc, _mm512_sub_epi64(lane, _mm512_set1_epi64(2)), values));
    values = _mm512_add_epi64(values, _mm512_maskz_permutexvar_epi64(0xf0, _mm512_sub_epi64(lane, _mm512_set1_epi64(4)), values));
    return values;
}

// Stable eight-bit radix passes over the key bytes that differ between keys, so a function's small positions and few
// classes take two or three passes of the eight (#2699). Event positions have the full u32 range: a dense counting
// table would make memory depend on the producer's position numbering. The ordered scatter is shared; its cost is
// included in preparation. Returns whether the sorted columns are the temporary ones: an odd number of passes ran.
static bool tier1_frame_sort(uint64_t* key, uint32_t* id, uint64_t* temporary_key, uint32_t* temporary_id, uint32_t count)
{
    uint64_t differing = 0;
    for (uint32_t i = 1; i < count; i += 1)
    {
        differing |= key[i] ^ key[0];
    }
    bool swapped = false;
    for (uint32_t shift = 0; shift < 64; shift += 8)
    {
        if (((differing >> shift) & 255) != 0)
        {
            uint32_t offsets[256] = {0};
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
            swapped = !swapped;
        }
    }
    return swapped;
}

// Preparation's sort of up to BUSTER_TIER1_FRAME_RANK_EVENTS events (#2699): an event's rank is the count of keys
// below its own, and every key is compared with eight others at a time. The keys are distinct, each event's class,
// position and input place (tier1_frame_prepare), so the ranks are the stable radix sort's order. `vectors` is a
// constant at each call, which keeps the keys and the ranks in registers; the lanes past `events` hold key 0, below
// nothing, and their ranks aren't read.
static inline __attribute__((always_inline)) void tier1_frame_rank_vectors(const uint64_t* key, uint32_t events, uint32_t vectors, uint32_t* rank)
{
    __m512i keys[BUSTER_TIER1_FRAME_RANK_EVENTS / 8];
    __m512i ranks[BUSTER_TIER1_FRAME_RANK_EVENTS / 8];
    for (uint32_t vector = 0; vector < vectors; vector += 1)
    {
        const uint32_t first = vector * 8;
        const uint32_t live = events <= first ? 0 : (events - first < 8 ? events - first : 8);
        keys[vector] = _mm512_maskz_loadu_epi64((__mmask8)((1u << live) - 1), key + (live != 0 ? first : 0));
        ranks[vector] = _mm512_setzero_si512();
    }
    for (uint32_t other = 0; other < events; other += 1)
    {
        const __m512i value = _mm512_set1_epi64((long long)key[other]);
        for (uint32_t vector = 0; vector < vectors; vector += 1)
        {
            ranks[vector] = _mm512_mask_add_epi64(ranks[vector], _mm512_cmpgt_epu64_mask(keys[vector], value), ranks[vector], _mm512_set1_epi64(1));
        }
    }
    for (uint32_t vector = 0; vector < vectors; vector += 1)
    {
        _mm256_storeu_si256((__m256i*)(rank + vector * 8), _mm512_cvtepi64_epi32(ranks[vector]));
    }
}

// `key` holds count ends' keys, then count starts'; `event` takes the sorted event IDs.
static void tier1_frame_rank(const uint64_t* key, uint32_t count, uint32_t* event)
{
    const uint32_t events = count * 2;
    uint32_t rank[BUSTER_TIER1_FRAME_RANK_EVENTS];
    if (events <= 8)
    {
        tier1_frame_rank_vectors(key, events, 1, rank);
    }
    else if (events <= 16)
    {
        tier1_frame_rank_vectors(key, events, 2, rank);
    }
    else if (events <= 32)
    {
        tier1_frame_rank_vectors(key, events, 4, rank);
    }
    else
    {
        tier1_frame_rank_vectors(key, events, 8, rank);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        event[rank[i]] = i * 2;
        event[rank[count + i]] = i * 2 + 1;
    }
}

// Preparation's classes while a function has at most eight (all but one of sqlite3.c's 1,034 frames with objects,
// #2699): the distinct class keys stay in one register, each item's key is compared with all of them at once, and a
// key's class is the count of distinct keys below it, its place in the sorted order. Each item takes its key's lane,
// which `remap` turns into its class. False at a ninth distinct key: the caller sorts instead, and keeps nothing of this.
static bool tier1_frame_classes(Tier1FrameWork* work, const uint64_t* key, uint32_t* remap)
{
    uint32_t* const object_class = work->object_class;
    // No key equals an unused lane's: a key's high half is UINT32_MAX less its alignment, which is at least 1.
    __m512i table = _mm512_set1_epi64(-1);
    uint32_t used = 0;
    bool fits = true;
    for (uint32_t i = 0; fits && i < work->count; i += 1)
    {
        const __m512i value = _mm512_set1_epi64((long long)key[i]);
        const uint32_t equal = _mm512_cmpeq_epi64_mask(table, value);
        if (equal != 0)
        {
            object_class[i] = (uint32_t)__builtin_ctz(equal);
        }
        else if (used < 8)
        {
            table = _mm512_mask_mov_epi64(table, (__mmask8)(1u << used), value);
            object_class[i] = used;
            used += 1;
        }
        else
        {
            fits = false;
        }
    }
    if (fits)
    {
        uint64_t distinct[8];
        _mm512_storeu_si512(distinct, table);
        for (uint32_t i = 0; i < used; i += 1)
        {
            const uint32_t group = (uint32_t)__builtin_popcount(_mm512_cmplt_epu64_mask(table, _mm512_set1_epi64((long long)distinct[i])));
            remap[i] = group;
            work->class_stride[group] = (uint32_t)distinct[i];
            work->class_alignment[group] = UINT32_MAX - (uint32_t)(distinct[i] >> 32);
        }
        work->class_count = used;
    }
    return fits;
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
static Tier1FrameError tier1_frame_rows(const Tier1FrameSlots* slots, uint64_t* keys, uint32_t* maximum_alignment)
{
    Tier1FrameError error = TIER1_FRAME_OK;
    uint32_t largest = 1;
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
        error = tier1_frame_rows(&request->slots, 0, &alignment);
    }
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_call_requirements(request, alignment, result);
    }
    return error;
}

static Tier1FrameError tier1_frame_prepare(const Tier1FrameRequest* request, Arena* scratch, Tier1FrameWork* result)
{
    const uint64_t mark = scratch->length;
    Tier1FrameError error = tier1_frame_header(request);
    Tier1FrameWork candidate = {
        .count = request->slots.count,
    };
    const uint32_t count = candidate.count;
    const uint32_t events = count * 2;
    uint64_t* key = 0;
    uint64_t* temporary_key = 0;
    uint32_t* temporary_event = 0;
    if (error == TIER1_FRAME_OK && count != 0)
    {
        // One push: the sort's two key columns and the classes' offsets, then the dword columns.
        const uint64_t dwords = (uint64_t)events * 2 + ((uint64_t)count + 1) * 2 + (uint64_t)count * 5;
        key = arena_push_array(scratch, (uint64_t)events * 2 + count + (dwords + 1) / 2, 8, 8);
        if (key == 0)
        {
            error = TIER1_FRAME_CAPACITY;
        }
        else
        {
            temporary_key = key + events;
            candidate.class_offset = temporary_key + events;
            candidate.event = (uint32_t*)(candidate.class_offset + count);
            temporary_event = candidate.event + events;
            candidate.last = temporary_event + events;
            candidate.color = candidate.last + count + 1;
            candidate.object_class = candidate.color + count + 1;
            candidate.class_stride = candidate.object_class + count;
            candidate.class_alignment = candidate.class_stride + count;
            candidate.class_colors = candidate.class_alignment + count;
            candidate.class_slot = candidate.class_colors + count;
        }
    }
    if (error == TIER1_FRAME_OK && request->slots.indices != 0 && count != 0)
    {
        // Distinct indices: each row is stamped with its item, and an item whose row holds another's stamp shares it.
        // Only stamped rows are read, so the column isn't cleared. Without room for a stamp a row, the indices are sorted.
        const uint32_t* const indices = request->slots.indices;
        const uint64_t stamps = scratch->length;
        uint32_t* const stamp = arena_push_array(scratch, request->slots.column_count, 4, 4);
        uint64_t different = 0;
        if (stamp != 0)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                stamp[indices[i]] = i;
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                different |= stamp[indices[i]] ^ i;
            }
            arena_pop_to(scratch, stamps);
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                key[i] = indices[i];
                candidate.event[i] = i;
            }
            const uint64_t* const sorted = tier1_frame_sort(key, candidate.event, temporary_key, temporary_event, count) ? temporary_key : key;
            for (uint32_t i = 1; i < count; i += 1)
            {
                different |= sorted[i] == sorted[i - 1];
            }
        }
        if (different != 0)
        {
            error = TIER1_FRAME_INVALID;
        }
    }
    uint32_t alignment = 1;
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_rows(&request->slots, key, &alignment);
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
        uint32_t lanes[8];
        const uint32_t* remap = 0;
        if (tier1_frame_classes(&candidate, key, lanes))
        {
            remap = lanes;
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                candidate.event[i] = i;
            }
            const bool swapped = tier1_frame_sort(key, candidate.event, temporary_key, temporary_event, count);
            const uint64_t* const sorted_key = swapped ? temporary_key : key;
            const uint32_t* const sorted_item = swapped ? temporary_event : candidate.event;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (i == 0 || sorted_key[i] != sorted_key[i - 1])
                {
                    const uint32_t group = candidate.class_count;
                    candidate.class_stride[group] = (uint32_t)sorted_key[i];
                    candidate.class_alignment[group] = UINT32_MAX - (uint32_t)(sorted_key[i] >> 32);
                    candidate.class_count += 1;
                }
                candidate.object_class[sorted_item[i]] = candidate.class_count - 1;
            }
        }
        // Either sort keeps all ends before starts at an equal position, allowing adjacent lifetimes to share, and
        // equal events in item order.
        if (events <= BUSTER_TIER1_FRAME_RANK_EVENTS)
        {
            // Distinct keys: under 64 events' places in 6 bits, a position in 32, and a class under 32.
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t row = tier1_frame_row(&request->slots, i);
                const uint32_t group = remap != 0 ? remap[candidate.object_class[i]] : candidate.object_class[i];
                candidate.object_class[i] = group;
                key[i] = (uint64_t)group << 38 | (uint64_t)request->slots.end[row] << 6 | i;
                key[count + i] = (uint64_t)group << 38 | (uint64_t)request->slots.start[row] << 6 | (count + i);
            }
            tier1_frame_rank(key, count, candidate.event);
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t row = tier1_frame_row(&request->slots, i);
                const uint32_t group = remap != 0 ? remap[candidate.object_class[i]] : candidate.object_class[i];
                candidate.object_class[i] = group;
                key[i] = (uint64_t)group << 32 | request->slots.end[row];
                candidate.event[i] = i * 2;
                key[count + i] = (uint64_t)group << 32 | request->slots.start[row];
                candidate.event[count + i] = i * 2 + 1;
            }
            if (tier1_frame_sort(key, candidate.event, temporary_key, temporary_event, events))
            {
                candidate.event = temporary_event;
            }
        }
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

// Each class uses exactly its maximum live pressure: the free colors are a stack, an end pushes its item's color and a
// start pops the last one pushed, or takes a new color, its pressure less one, when the pressure is a new maximum of
// its class. The stack's place is the pressure level, a start's pressure after it and an end's before it, and what it
// holds is the last item that ended there, whose color the start reads; a class's levels are written by its own ends
// before its starts read them, since its pressure rose to the level before and has fallen below it since.
//
// This is one ordered pass, with no branch on the event's kind: a start stores its color and an end its
// item through one selected address. The pass is the part that stays scalar (#2699). Its chain is the stack: a color
// goes from an item to the next item of its slot, so pointer jumping, the data-parallel form (test-tier1-frame's
// oracle, tier1_frame_test_color_jumping), does the pass's work once per round.
static void tier1_frame_color(Tier1FrameWork* work)
{
    const uint32_t count = work->count;
    const uint32_t events = count * 2;
    const uint32_t* const event = work->event;
    const uint32_t* const object_class = work->object_class;
    uint32_t* const last = work->last;
    uint32_t* const color = work->color;
    uint32_t* const class_colors = work->class_colors;
    if (events != 0)
    {
        // Every level names an item: a level read before its class wrote it gives a color that isn't taken.
        memset(last, 0, ((uint64_t)count + 1) * 4);
    }
    // Every class's events balance, so each class starts at pressure 0.
    uint32_t pressure = 0;
    uint32_t maximum = 0;
    uint32_t previous_group = 0;
    for (uint32_t i = 0; i < events; i += 1)
    {
        const uint32_t item = event[i] >> 1;
        const uint32_t start = event[i] & 1;
        const uint32_t group = object_class[item];
        const uint32_t level = pressure + start;
        maximum = group != previous_group ? 0 : maximum;
        previous_group = group;
        // Only a start's level passes the maximum: an end's is the pressure before it.
        const bool root = level > maximum;
        maximum = root ? level : maximum;
        class_colors[group] = maximum;
        // Selects, not branches: the kinds and the roots follow the lifetimes, which nothing predicts. Each event makes
        // both stores, a start's color and an end's item, one of them to a spare place (color[count], and last[0], which
        // is no level's), so that an end's item doesn't wait for the color only a start takes.
        const uint32_t inherited = color[last[level]];
        color[__builtin_unpredictable(start != 0) ? item : count] = __builtin_unpredictable(root) ? level - 1 : inherited;
        last[__builtin_unpredictable(start != 0) ? 0 : level] = item;
        pressure += start * 2 - 1;
    }
}

static void tier1_frame_step(Tier1FrameStep* steps, uint32_t* count, Tier1FrameStepKind kind, uint32_t reg, uint32_t base, int32_t offset, uint32_t amount)
{
    // At most 25 prologue and 8 epilogue steps: one pointer push/set, six register pushes, one allocation, one align,
    // one stable-base set and fourteen variadic saves. These are per-function fixed bounds, not input-sized lists.
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

// Tier 0's frame protocol (the file's comment), which the unwind tables describe from the transitions' PCs.
static void tier1_frame_steps(const Tier1FrameRequest* request, const Tier1FrameRequirements* requirements, Tier1Frame* frame)
{
    const bool probe = (request->flags & BUSTER_TIER1_FRAME_PROBE_STACK) != 0;
    const uint32_t pushes = (uint32_t)__builtin_popcount(frame->saved_registers);
    if (frame->frame_pointer)
    {
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_PUSH_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, -8, 8);
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SET_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, 0, 0);
    }
    // Ascending encoder numbers are the unwind order: RBX, RBP, R12, R13, R14, R15.
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        if ((frame->saved_registers & (1u << reg)) != 0)
        {
            tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_PUSH_REGISTER, reg, BUSTER_ABI_RSP, -8, 8);
        }
    }
    // Under PROBE_STACK a realigned frame probes its subtraction whatever its size, so that the alignment gap's probe
    // starts from a touched address: the two together could otherwise cross an unprobed page. So does a frame whose body
    // moves rsp (#177, the dynamic stack's): its dynamic allocations and call areas start from rsp, which an unprobed
    // subtraction under a page would leave up to a page below the last touch, and an allocation after it could then take
    // rsp past a guard page before its first touch (LLVM's constraint). A frame pointer the options keep doesn't (#1450):
    // without a dynamic stack less than a page stays untouched until the next call's return address.
    if (frame->stack_adjust_bytes != 0)
    {
        const bool probed = probe && (frame->stack_adjust_bytes >= BUSTER_TIER1_FRAME_PAGE_BYTES || frame->realigned || requirements->dynamic_stack);
        tier1_frame_step(frame->prologue, &frame->prologue_count, probed ? TIER1_FRAME_PROBE_ALLOCATE : TIER1_FRAME_ALLOCATE,
            BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->stack_adjust_bytes);
    }
    if (frame->realigned)
    {
        tier1_frame_step(frame->prologue, &frame->prologue_count, probe ? TIER1_FRAME_PROBE_ALIGN : TIER1_FRAME_ALIGN_STACK, BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->alignment);
    }
    if (frame->base_register == BUSTER_TIER1_FRAME_REALIGN_BASE && requirements->dynamic_stack && frame->realigned)
    {
        tier1_frame_step(frame->prologue, &frame->prologue_count, TIER1_FRAME_SET_REALIGN_BASE, BUSTER_TIER1_FRAME_REALIGN_BASE, BUSTER_ABI_RSP, 0, 0);
    }
    // Every variadic store addresses RSP; a dynamic frame's RBP-relative displacements differ from RSP-relative ones by
    // the pushes and the fixed subtraction below rbp.
    const int64_t to_rsp = frame->base_register == BUSTER_ABI_RBP ? (int64_t)frame->stack_adjust_bytes + 8 * pushes : 0;
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
    if (frame->frame_pointer)
    {
        // rsp back to the last push, past whatever the body allocated or realigned: `lea rsp, [rbp - 8n]`, or with no
        // pushes `mov rsp, rbp`.
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RESET_STACK, BUSTER_ABI_RSP, BUSTER_ABI_RBP, -(int32_t)(8 * pushes), 0);
    }
    else if (frame->stack_adjust_bytes != 0)
    {
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_RELEASE, BUSTER_ABI_RSP, BUSTER_ABI_RSP, 0, frame->stack_adjust_bytes);
    }
    for (uint32_t reg = 16; reg > 0; reg -= 1)
    {
        if ((frame->saved_registers & (1u << (reg - 1))) != 0)
        {
            tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_POP_REGISTER, reg - 1, BUSTER_ABI_RSP, 0, 8);
        }
    }
    if (frame->frame_pointer)
    {
        tier1_frame_step(frame->epilogue, &frame->epilogue_count, TIER1_FRAME_POP_POINTER, BUSTER_ABI_RBP, BUSTER_ABI_RSP, 0, 8);
    }
}

static Tier1FrameError tier1_frame_layout(const Tier1FrameRequest* request, Tier1FrameWork* work, Arena* output, Tier1Frame* result)
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
        .probe_stack = (request->flags & BUSTER_TIER1_FRAME_PROBE_STACK) != 0,
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
    uint64_t position = object_begin + object_bytes;
    const uint64_t variadic_begin = candidate.variadic ? tier1_frame_align(position, 16) : 0;
    if (candidate.variadic)
    {
        position = variadic_begin + BUSTER_ABI_SAVE_AREA_BYTES;
    }
    // The pushes, rbp's and the callee-saved registers', come before the subtraction, so rsp after them is entry RSP (8
    // modulo 16) less 8 per push: 0 or 8 modulo 16.
    const uint32_t pushes = (uint32_t)__builtin_popcount(candidate.saved_registers);
    const uint64_t pushed = 8 * (uint64_t)pushes + (candidate.frame_pointer ? 8 : 0);
    const uint32_t pushed_residue = (uint32_t)((8 + pushed) & 15);
    uint32_t data_alignment = requirements.object_alignment;
    data_alignment = candidate.variadic && data_alignment < 16 ? 16 : data_alignment;
    // A red-zone base must compensate for that residue, including sixteen-byte objects. The red zone is below the
    // pushes' rsp.
    const uint32_t residue = data_alignment <= 16 ? pushed_residue % data_alignment : 0;
    const uint64_t red_span = tier1_frame_align(position + residue, data_alignment) - residue;
    candidate.red_zone = request->call_count == 0 && !candidate.frame_pointer && !requirements.dynamic_stack &&
        (request->flags & BUSTER_TIER1_FRAME_NO_RED_ZONE) == 0 && red_span <= BUSTER_ABI_RED_ZONE_BYTES;
    // The subtraction leaves rsp 16-aligned; a realigned frame's `and` after it aligns it further, by at most its
    // alignment less 16.
    uint64_t adjustment = 0;
    if (!candidate.red_zone && (position != 0 || request->call_count != 0))
    {
        adjustment = tier1_frame_align(position + pushed_residue, 16) - pushed_residue;
    }
    const uint64_t maximum = pushed + (candidate.red_zone ? red_span : adjustment + (candidate.realigned ? requirements.alignment - 16 : 0));
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
        // RBP is the pushes and the subtraction above the post-prologue rsp.
        const int64_t bias = candidate.red_zone ? -(int64_t)red_span : (candidate.base_register == BUSTER_ABI_RBP ? -(int64_t)(adjustment + 8 * pushes) : 0);
        for (uint32_t i = 0; i < work->count; i += 1)
        {
            const uint32_t group = work->object_class[i];
            candidate.slot[i] = work->class_slot[group] + work->color[i];
            candidate.offset[i] = (int32_t)((int64_t)(work->class_offset[group] + (uint64_t)work->color[i] * work->class_stride[group]) + bias);
        }
        candidate.variadic_offset = candidate.variadic ? (int32_t)((int64_t)variadic_begin + bias) : 0;
        // The k-th push's word is 8 (k + 1) bytes below rbp, or below entry RSP, which is the subtraction and the pushes
        // above the post-prologue rsp.
        uint32_t saved_index = 0;
        for (uint32_t reg = 0; reg < 16; reg += 1)
        {
            if ((candidate.saved_registers & (1u << reg)) != 0)
            {
                const int64_t below = 8 * (int64_t)(saved_index + 1);
                candidate.save_offset[reg] = (int32_t)(candidate.frame_pointer ? -below : (int64_t)adjustment + 8 * (int64_t)pushes - below);
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

static Tier1FrameError tier1_frame_plan(const Tier1FrameRequest* request, Arena* output, Arena* scratch, Tier1Frame* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1FrameError error = output == scratch ? TIER1_FRAME_INVALID : TIER1_FRAME_OK;
    Tier1FrameWork work;
    if (error == TIER1_FRAME_OK)
    {
        error = tier1_frame_prepare(request, scratch, &work);
    }
    if (error == TIER1_FRAME_OK)
    {
        tier1_frame_color(&work);
        error = tier1_frame_layout(request, &work, output, result);
    }
    arena_pop_to(scratch, scratch_mark);
    if (error != TIER1_FRAME_OK)
    {
        arena_pop_to(output, output_mark);
    }
    return error;
}
