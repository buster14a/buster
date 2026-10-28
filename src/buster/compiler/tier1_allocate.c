// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's machine-interval allocator. Include after lane.c. The selector supplies effective uses after folding;
// its liveness adapter supplies half-open ranges in loop-contiguous machine order. This is not a second IR.
// tier1_allocate builds position events, colors unrestricted classes by pressure/reuse chains and pointer jumping,
// and defers constrained or overfull classes to tier1_allocate_scan. The scalar twin always uses that scan.
// tier1_allocation_verify independently checks fragments, interference and fixed ranges. tier1_resolve_moves orders
// edge/phi assignments, breaking cycles with a caller-reserved location. No function emits spill machine code.
//
// Columns are borrowed, unpadded, immutable, and may be unaligned. Output and temporary memory belong to one arena;
// inputs and result must not alias its unused capacity. Failure restores its length and leaves the result unchanged.
// Without register-required uses there are at most two fragments per interval. With required uses a spilled value
// reenters a register at its next required use and may be evicted again. Separate input intervals of the same value
// represent holes or machine-constraint splits; their transitions are the producer's.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#define BUSTER_TIER1_ALLOCATE_MAX_INTERVALS (1u << 20)
#define BUSTER_TIER1_ALLOCATE_MAX_POSITIONS (1u << 24)
#define BUSTER_TIER1_ALLOCATE_MAX_USES (1u << 24)
#define BUSTER_TIER1_ALLOCATE_USE_EVENT 0x80000000u
#define BUSTER_TIER1_ALLOCATE_CLASSES 2
#define BUSTER_TIER1_ALLOCATE_GPR 0
#define BUSTER_TIER1_ALLOCATE_VECTOR 1
#define BUSTER_TIER1_ALLOCATE_REGISTER 0
#define BUSTER_TIER1_ALLOCATE_SPILL 1
#define BUSTER_TIER1_ALLOCATE_REMATERIALIZE 2
#define BUSTER_TIER1_ALLOCATE_REQUIRED 1
#define BUSTER_TIER1_ALLOCATE_NO_REGISTER UINT8_MAX

typedef struct Tier1FixedIntervals Tier1FixedIntervals;
struct Tier1FixedIntervals
{
    const uint32_t* starts;
    const uint32_t* ends;
    const uint32_t* registers;
    const uint8_t* classes;
    uint32_t count;
};

typedef struct Tier1AllocationInput Tier1AllocationInput;
struct Tier1AllocationInput
{
    const uint32_t* starts;
    const uint32_t* ends;
    const uint32_t* values;
    const uint32_t* allowed_registers;
    const uint8_t* classes;
    // Optional columns. Required means the entire interval must have a register; impossible requests fail closed.
    const uint8_t* flags;
    const uint8_t* hints;
    const uint8_t* rematerializable;
    // Both use columns may be absent (next use then conservatively means interval end). Otherwise each slice is
    // sorted, contains positions inside its interval, and may be empty. Duplicate positions are legal.
    const uint32_t* use_offsets;
    const uint32_t* use_positions;
    // Optional byte per use: 1 requires a physical register at that position, 0 permits a memory/rematerialized
    // operand. Required uses trigger reload/resplit in the scalar path. A position denotes one simultaneous phase;
    // a producer uses separate input/output phases when the instruction may reuse a dying input register.
    const uint8_t* use_required;
    uint32_t use_count;
    uint32_t count;
    uint32_t value_count;
    uint32_t position_count;
    // Physical register bits. GPR bits 0..15; vector bits 0..15 or 0..31 as permitted by the output target.
    // The producer excludes RSP and any frame/scratch registers. A zero class mask permits only spills.
    uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    Tier1FixedIntervals fixed;
};

typedef enum Tier1AllocationError
{
    TIER1_ALLOCATION_OK,
    TIER1_ALLOCATION_INVALID,
    TIER1_ALLOCATION_CAPACITY,
    TIER1_ALLOCATION_CONSTRAINT,
} Tier1AllocationError;

typedef struct Tier1Allocation Tier1Allocation;
struct Tier1Allocation
{
    // Interval i owns fragments [fragment_offsets[i], fragment_offsets[i + 1]), in position order.
    uint32_t* fragment_offsets;
    uint32_t* starts;
    uint32_t* ends;
    uint8_t* kinds;
    uint8_t* registers;
    uint32_t fragment_count;
    uint32_t interval_count;
    // A direct selector view: UINT8_MAX unless every fragment of this value has one identical register.
    uint8_t* value_registers;
    uint32_t* spill_ids;
    uint32_t* spill_values;
    uint32_t spill_count;
    uint32_t vector_intervals;
    uint32_t scalar_intervals;
    uint32_t splits;
    uint32_t peak_pressure[BUSTER_TIER1_ALLOCATE_CLASSES];
    uint64_t retained_bytes;
    uint64_t scratch_bytes;
};

typedef struct Tier1AllocationLocation Tier1AllocationLocation;
struct Tier1AllocationLocation
{
    uint32_t spill_id;
    uint8_t kind;
    uint8_t reg;
};

static bool tier1_allocation_input_valid(const Tier1AllocationInput* input)
{
    bool valid = input != 0;
    if (valid)
    {
        valid = input->count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && input->value_count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && input->position_count <= BUSTER_TIER1_ALLOCATE_MAX_POSITIONS && input->fixed.count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && input->use_count <= BUSTER_TIER1_ALLOCATE_MAX_USES;
        valid = valid && (input->registers[0] & 0xffff0000u) == 0 && (input->count == 0 || (input->starts != 0 && input->ends != 0 && input->values != 0 && input->allowed_registers != 0 && input->classes != 0));
        valid = valid && (input->fixed.count == 0 || (input->fixed.starts != 0 && input->fixed.ends != 0 && input->fixed.registers != 0 && input->fixed.classes != 0));
        valid = valid && ((input->use_offsets == 0 && input->use_positions == 0 && input->use_count == 0) || (input->use_offsets != 0 && (input->use_count == 0 || input->use_positions != 0)));
        valid = valid && (input->use_required == 0 || input->use_offsets != 0);
        if (valid && input->use_offsets != 0)
        {
            valid = input->use_offsets[0] == 0 && input->use_offsets[input->count] == input->use_count;
        }
    }
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        valid = input->starts[i] < input->ends[i] && input->ends[i] <= input->position_count && input->values[i] < input->value_count && input->classes[i] < BUSTER_TIER1_ALLOCATE_CLASSES;
        valid = valid && (input->flags == 0 || (input->flags[i] & ~BUSTER_TIER1_ALLOCATE_REQUIRED) == 0) && (input->rematerializable == 0 || input->rematerializable[i] <= 1);
        if (valid)
        {
            valid = (input->allowed_registers[i] & ~input->registers[input->classes[i]]) == 0;
            valid = valid && (input->hints == 0 || input->hints[i] == UINT8_MAX || (input->hints[i] < 32 && ((input->allowed_registers[i] >> input->hints[i]) & 1) != 0));
        }
        if (valid && input->use_offsets != 0)
        {
            const uint32_t begin = input->use_offsets[i];
            const uint32_t end = input->use_offsets[i + 1];
            valid = begin <= end && end <= input->use_count;
            for (uint32_t use = begin; valid && use < end; use += 1)
            {
                const uint32_t position = input->use_positions[use];
                valid = position >= input->starts[i] && position < input->ends[i] && (use == begin || position >= input->use_positions[use - 1]);
                valid = valid && (input->use_required == 0 || input->use_required[use] <= 1);
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->fixed.count; i += 1)
    {
        valid = input->fixed.starts[i] < input->fixed.ends[i] && input->fixed.ends[i] <= input->position_count && input->fixed.classes[i] < BUSTER_TIER1_ALLOCATE_CLASSES;
        valid = valid && (input->fixed.classes[i] != BUSTER_TIER1_ALLOCATE_GPR || (input->fixed.registers[i] & 0xffff0000u) == 0);
    }
    return valid;
}

static uint32_t tier1_allocation_next_use(const Tier1AllocationInput* input, uint32_t interval, uint32_t position)
{
    uint32_t result = input->ends[interval];
    if (input->use_offsets != 0)
    {
        uint32_t first = input->use_offsets[interval];
        uint32_t end = input->use_offsets[interval + 1];
        while (first < end)
        {
            const uint32_t middle = first + (end - first) / 2;
            if (input->use_positions[middle] < position)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        if (first < input->use_offsets[interval + 1])
        {
            result = input->use_positions[first];
        }
    }
    return result;
}

static uint8_t tier1_allocation_spill_kind(const Tier1AllocationInput* input, uint32_t interval)
{
    return input->rematerializable != 0 && input->rematerializable[interval] != 0 ? BUSTER_TIER1_ALLOCATE_REMATERIALIZE : BUSTER_TIER1_ALLOCATE_SPILL;
}

// Scalar decisions are bounded by 32 physical registers, not by the number of previously seen intervals.
// Fixed clobbers restrict a whole interval; splitting around call sites belongs in the producer's machine ranges.
static bool tier1_allocate_scan(const Tier1AllocationInput* input, uint32_t class, const uint32_t* events, uint32_t event_count, const uint32_t* allowed, uint8_t* regs, uint32_t* split, uint8_t* kinds)
{
    uint32_t active[32];
    for (uint32_t r = 0; r < 32; r += 1)
    {
        active[r] = UINT32_MAX;
    }
    bool valid = true;
    for (uint32_t e = 0; valid && e < event_count; e += 1)
    {
        const uint32_t interval = events[e] >> 1;
        if ((events[e] & BUSTER_TIER1_ALLOCATE_USE_EVENT) == 0 && (events[e] & 1) != 0 && input->classes[interval] == class)
        {
            const uint32_t position = input->starts[interval];
            uint32_t free = 0;
            for (uint32_t r = 0; r < 32; r += 1)
            {
                if (active[r] != UINT32_MAX && input->ends[active[r]] <= position)
                {
                    active[r] = UINT32_MAX;
                }
                free |= active[r] == UINT32_MAX ? 1u << r : 0;
            }
            free &= allowed[interval];
            uint32_t chosen = UINT32_MAX;
            if (free != 0)
            {
                chosen = (uint32_t)__builtin_ctz(free);
                if (input->hints != 0 && input->hints[interval] != UINT8_MAX && ((free >> input->hints[interval]) & 1) != 0)
                {
                    chosen = input->hints[interval];
                }
            }
            else
            {
                const bool required = input->flags != 0 && (input->flags[interval] & BUSTER_TIER1_ALLOCATE_REQUIRED) != 0;
                uint32_t farthest = tier1_allocation_next_use(input, interval, position);
                for (uint32_t r = 0; r < 32; r += 1)
                {
                    const uint32_t other = active[r];
                    if (((allowed[interval] >> r) & 1) != 0 && other != UINT32_MAX && (input->flags == 0 || (input->flags[other] & BUSTER_TIER1_ALLOCATE_REQUIRED) == 0))
                    {
                        const uint32_t next = tier1_allocation_next_use(input, other, position);
                        if ((required && chosen == UINT32_MAX) || next > farthest)
                        {
                            chosen = r;
                            farthest = next;
                        }
                    }
                }
                if (chosen != UINT32_MAX)
                {
                    const uint32_t victim = active[chosen];
                    split[victim] = position;
                    kinds[victim] = tier1_allocation_spill_kind(input, victim);
                }
                else if (required)
                {
                    valid = false;
                }
            }
            if (chosen != UINT32_MAX)
            {
                regs[interval] = (uint8_t)chosen;
                active[chosen] = interval;
            }
            else
            {
                split[interval] = position;
                kinds[interval] = tier1_allocation_spill_kind(input, interval);
            }
        }
    }
    return valid;
}

typedef struct Tier1AllocationPiece Tier1AllocationPiece;
struct Tier1AllocationPiece
{
    uint32_t start;
    uint32_t end;
    uint32_t next;
    uint8_t reg;
    uint8_t kind;
};

typedef struct Tier1AllocationPieces Tier1AllocationPieces;
struct Tier1AllocationPieces
{
    Tier1AllocationPiece* pieces;
    uint32_t* heads;
    uint32_t* tails;
    uint32_t count;
};

static void tier1_allocation_finish_piece(Tier1AllocationPieces* pieces, uint32_t interval, uint32_t start, uint32_t end, uint8_t kind, uint8_t reg)
{
    if (start < end)
    {
        const uint32_t id = pieces->count;
        pieces->count += 1;
        pieces->pieces[id] = (Tier1AllocationPiece){
            .start = start,
            .end = end,
            .next = UINT32_MAX,
            .reg = reg,
            .kind = kind,
        };
        if (pieces->heads[interval] == UINT32_MAX)
        {
            pieces->heads[interval] = id;
        }
        else
        {
            pieces->pieces[pieces->tails[interval]].next = id;
        }
        pieces->tails[interval] = id;
    }
}

static bool tier1_allocation_required_at(const Tier1AllocationInput* input, uint32_t interval, uint32_t position)
{
    bool required = input->flags != 0 && (input->flags[interval] & BUSTER_TIER1_ALLOCATE_REQUIRED) != 0;
    if (!required && input->use_required != 0)
    {
        uint32_t first = input->use_offsets[interval];
        uint32_t end = input->use_offsets[interval + 1];
        while (first < end)
        {
            const uint32_t middle = first + (end - first) / 2;
            if (input->use_positions[middle] < position)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        while (first < input->use_offsets[interval + 1] && input->use_positions[first] == position)
        {
            required |= input->use_required[first] != 0;
            first += 1;
        }
    }
    return required;
}

// Reload at required uses, with the same next-use eviction rule as the simpler scan. All values needed at the
// current position are pinned before processing any one event, so input order cannot evict a simultaneous operand.
// Each start/use creates at most two transitions (its own and its victim's), bounding the temporary piece capacity.
static bool tier1_allocate_scan_uses(const Tier1AllocationInput* input, uint32_t class, const uint32_t* events, uint32_t event_count, const uint32_t* use_intervals, const uint32_t* allowed, uint8_t* regs, uint32_t* begins, uint8_t* kinds, Tier1AllocationPieces* pieces)
{
    uint32_t active[32];
    for (uint32_t r = 0; r < 32; r += 1)
    {
        active[r] = UINT32_MAX;
    }
    for (uint32_t i = 0; i < input->count; i += 1)
    {
        if (input->classes[i] == class)
        {
            begins[i] = input->starts[i];
            kinds[i] = tier1_allocation_spill_kind(input, i);
        }
    }
    bool valid = true;
    for (uint32_t e = 0; valid && e < event_count; e += 1)
    {
        const bool use_event = (events[e] & BUSTER_TIER1_ALLOCATE_USE_EVENT) != 0;
        const uint32_t use = events[e] & ~BUSTER_TIER1_ALLOCATE_USE_EVENT;
        const uint32_t interval = use_event ? use_intervals[use] : events[e] >> 1;
        const bool handles = use_event || (events[e] & 1) != 0;
        if (handles && input->classes[interval] == class && (!use_event || kinds[interval] != BUSTER_TIER1_ALLOCATE_REGISTER))
        {
            const uint32_t position = use_event ? input->use_positions[use] : input->starts[interval];
            const bool required = tier1_allocation_required_at(input, interval, position);
            uint32_t free = 0;
            for (uint32_t r = 0; r < 32; r += 1)
            {
                if (active[r] != UINT32_MAX && input->ends[active[r]] <= position)
                {
                    active[r] = UINT32_MAX;
                }
                free |= active[r] == UINT32_MAX ? 1u << r : 0;
            }
            free &= allowed[interval];
            uint32_t chosen = UINT32_MAX;
            if (free != 0)
            {
                chosen = (uint32_t)__builtin_ctz(free);
                if (input->hints != 0 && input->hints[interval] != UINT8_MAX && ((free >> input->hints[interval]) & 1) != 0)
                {
                    chosen = input->hints[interval];
                }
            }
            else
            {
                uint32_t farthest = tier1_allocation_next_use(input, interval, position);
                for (uint32_t r = 0; r < 32; r += 1)
                {
                    const uint32_t victim = active[r];
                    if (((allowed[interval] >> r) & 1) != 0 && victim != UINT32_MAX && !tier1_allocation_required_at(input, victim, position))
                    {
                        const uint32_t next = tier1_allocation_next_use(input, victim, position);
                        if ((required && chosen == UINT32_MAX) || next > farthest)
                        {
                            chosen = r;
                            farthest = next;
                        }
                    }
                }
                if (chosen != UINT32_MAX)
                {
                    const uint32_t victim = active[chosen];
                    tier1_allocation_finish_piece(pieces, victim, begins[victim], position, kinds[victim], regs[victim]);
                    begins[victim] = position;
                    kinds[victim] = tier1_allocation_spill_kind(input, victim);
                    regs[victim] = UINT8_MAX;
                }
                else if (required)
                {
                    valid = false;
                }
            }
            if (chosen != UINT32_MAX)
            {
                tier1_allocation_finish_piece(pieces, interval, begins[interval], position, kinds[interval], regs[interval]);
                begins[interval] = position;
                kinds[interval] = BUSTER_TIER1_ALLOCATE_REGISTER;
                regs[interval] = (uint8_t)chosen;
                active[chosen] = interval;
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        if (input->classes[i] == class)
        {
            tier1_allocation_finish_piece(pieces, i, begins[i], input->ends[i], kinds[i], regs[i]);
        }
    }
    return valid;
}

// Inclusive prefix sums of +/-1 event columns, including bounded tails. Keeping the pressure column also gives
// the matching depth: an end at depth d opens the reuse chain consumed by the next start from depth d.
static void tier1_allocation_pressure(const int32_t* deltas, uint32_t count, int32_t* pressure)
{
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    int32_t carry = 0;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const uint32_t length = remaining < 16 ? remaining : 16;
        const __mmask16 mask = (__mmask16)((1u << length) - 1);
        __m512i sum = _mm512_maskz_loadu_epi32(mask, deltas + first);
        for (uint32_t step = 1; step < 16; step *= 2)
        {
            const __m512i offset = _mm512_set1_epi32((int32_t)step);
            const __mmask16 high = _mm512_cmpge_epi32_mask(lanes, offset);
            const __m512i previous = _mm512_maskz_permutexvar_epi32(high, _mm512_sub_epi32(lanes, offset), sum);
            sum = _mm512_add_epi32(sum, previous);
        }
        sum = _mm512_add_epi32(sum, _mm512_set1_epi32(carry));
        _mm512_mask_storeu_epi32(pressure + first, mask, sum);
        carry = pressure[first + length - 1];
    }
}

static void tier1_allocation_color(const Tier1AllocationInput* input, uint32_t class, const uint32_t* events, const int32_t* pressure, uint32_t event_count, uint32_t* parents, uint32_t* next, uint8_t* regs)
{
    uint32_t ended[32];
    uint32_t available = input->registers[class];
    for (uint32_t r = 0; r < 32; r += 1)
    {
        ended[r] = UINT32_MAX;
    }
    for (uint32_t e = 0; e < event_count; e += 1)
    {
        const uint32_t interval = events[e] >> 1;
        if ((events[e] & 1) == 0)
        {
            ended[pressure[e]] = interval;
        }
        else
        {
            const uint32_t depth = (uint32_t)pressure[e] - 1;
            if (ended[depth] != UINT32_MAX)
            {
                parents[interval] = ended[depth];
                ended[depth] = UINT32_MAX;
            }
            else
            {
                const uint32_t r = (uint32_t)__builtin_ctz(available);
                available &= available - 1;
                regs[interval] = (uint8_t)r;
            }
        }
    }
    // Parent ids can point in either numeric direction because intervals arrive in arbitrary input order. Two
    // disjoint columns make each round a simultaneous jump; scalar loads avoid Zen 5's expensive vector gather.
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t first = 0; first < input->count; first += 16)
        {
            const uint32_t remaining = input->count - first;
            const uint32_t length = remaining < 16 ? remaining : 16;
            uint32_t jumped[16] = { 0 };
            for (uint32_t lane = 0; lane < length; lane += 1)
            {
                jumped[lane] = parents[parents[first + lane]];
            }
            const __mmask16 mask = (__mmask16)((1u << length) - 1);
            const __m512i old = _mm512_maskz_loadu_epi32(mask, parents + first);
            const __m512i value = _mm512_loadu_si512(jumped);
            changed |= (_mm512_cmpneq_epi32_mask(old, value) & mask) != 0;
            _mm512_mask_storeu_epi32(next + first, mask, value);
        }
        memcpy(parents, next, (uint64_t)input->count * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < input->count; i += 1)
    {
        if (input->classes[i] == class)
        {
            regs[i] = regs[parents[i]];
        }
    }
}

// Keep the cross-product for tiny fixed sets and as an independent scalar reference. All borrowed tails are masked.
static void tier1_allocation_filter_fixed_simple(const Tier1AllocationInput* input, bool vector, uint32_t* allowed)
{
    for (uint32_t f = 0; f < input->fixed.count; f += 1)
    {
        for (uint32_t first = 0; first < input->count; first += vector ? 16 : 1)
        {
            if (vector)
            {
                const uint32_t remaining = input->count - first;
                const uint32_t length = remaining < 16 ? remaining : 16;
                const __mmask16 lanes = (__mmask16)((1u << length) - 1);
                const __m512i start = _mm512_maskz_loadu_epi32(lanes, input->starts + first);
                const __m512i end = _mm512_maskz_loadu_epi32(lanes, input->ends + first);
                const __m128i classes = _mm_maskz_loadu_epi8(lanes, input->classes + first);
                const __mmask16 same = _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8((char)input->fixed.classes[f]));
                const __mmask16 overlap = lanes & same & _mm512_cmplt_epu32_mask(start, _mm512_set1_epi32((int32_t)input->fixed.ends[f])) & _mm512_cmpgt_epu32_mask(end, _mm512_set1_epi32((int32_t)input->fixed.starts[f]));
                const __m512i current = _mm512_maskz_loadu_epi32(lanes, allowed + first);
                const __m512i trimmed = _mm512_andnot_si512(_mm512_set1_epi32((int32_t)input->fixed.registers[f]), current);
                _mm512_mask_storeu_epi32(allowed + first, overlap, trimmed);
            }
            else if (input->classes[first] == input->fixed.classes[f] && input->starts[first] < input->fixed.ends[f] && input->ends[first] > input->fixed.starts[f])
            {
                allowed[first] &= ~input->fixed.registers[f];
            }
        }
    }
}

// Query intervals in end-event order. Before [start,end)'s query, incorporate exactly fixed.starts < end;
// register r overlaps iff its prefix maximum fixed end is > start. Ends at start and starts at end do not overlap.
// Reuse the dead event buckets as start-position lists. Work is O(positions + fixed + events), with 32 register
// lanes per class; scratch is one u32 per fixed range. Required-use/start events do not query or advance the sweep.
static bool tier1_allocation_filter_fixed(const Tier1AllocationInput* input, Arena* arena, bool vector, const uint32_t* events, uint32_t event_count, uint32_t* buckets, uint32_t* allowed)
{
    bool valid = true;
    if (input->fixed.count != 0 && input->count != 0)
    {
        // Avoid index overhead when the cross-product fits sixteen comparisons per input row plus the already-paid
        // position domain. This keeps tiny functions/small fixed sets simple while bounding that work linearly too.
        const uint64_t simple_budget = input->position_count + 16ull * (input->count + input->fixed.count);
        const bool simple = !vector || (uint64_t)input->fixed.count * input->count <= simple_budget;
        if (simple)
        {
            tier1_allocation_filter_fixed_simple(input, vector, allowed);
        }
        else
        {
            uint32_t* next = arena_push_array(arena, input->fixed.count, sizeof(uint32_t), 4);
            valid = next != 0;
            if (valid)
            {
                uint32_t maximum_ends[BUSTER_TIER1_ALLOCATE_CLASSES][32] = { 0 };
                memset(buckets, 0, ((uint64_t)input->position_count + 2) * sizeof(uint32_t));
                for (uint32_t f = 0; f < input->fixed.count; f += 1)
                {
                    const uint32_t start = input->fixed.starts[f];
                    next[f] = buckets[start];
                    buckets[start] = f + 1;
                }
                uint32_t position = 0;
                for (uint32_t e = 0; e < event_count; e += 1)
                {
                    if ((events[e] & (BUSTER_TIER1_ALLOCATE_USE_EVENT | 1)) == 0)
                    {
                        const uint32_t i = events[e] >> 1;
                        while (position < input->ends[i])
                        {
                            for (uint32_t link = buckets[position]; link != 0; link = next[link - 1])
                            {
                                const uint32_t f = link - 1;
                                uint32_t* maximum = maximum_ends[input->fixed.classes[f]];
                                const __m512i end = _mm512_set1_epi32((int32_t)input->fixed.ends[f]);
                                for (uint32_t r = 0; r < 32; r += 16)
                                {
                                    const __m512i previous = _mm512_loadu_si512(maximum + r);
                                    const __m512i updated = _mm512_mask_max_epu32(previous, (__mmask16)(input->fixed.registers[f] >> r), previous, end);
                                    _mm512_storeu_si512(maximum + r, updated);
                                }
                            }
                            position += 1;
                        }
                        const uint32_t* maximum = maximum_ends[input->classes[i]];
                        const __m512i start = _mm512_set1_epi32((int32_t)input->starts[i]);
                        const uint32_t low = _mm512_cmpgt_epu32_mask(_mm512_loadu_si512(maximum), start);
                        const uint32_t high = _mm512_cmpgt_epu32_mask(_mm512_loadu_si512(maximum + 16), start);
                        allowed[i] &= ~(low | (high << 16));
                    }
                }
            }
        }
    }
    return valid;
}

static Tier1AllocationError tier1_allocate_impl(const Tier1AllocationInput* input, Arena* arena, bool vector, Tier1Allocation* result)
{
    Tier1AllocationError error = TIER1_ALLOCATION_INVALID;
    const uint64_t saved = arena != 0 ? arena->length : 0;
    Tier1Allocation allocation = { 0 };
    uint64_t retained = saved;
    uint32_t* events = 0;
    uint32_t* buckets = 0;
    uint32_t* allowed = 0;
    uint32_t* split = 0;
    uint32_t* parents = 0;
    uint32_t* next = 0;
    uint32_t* class_events = 0;
    int32_t* deltas = 0;
    int32_t* pressure = 0;
    uint8_t* regs = 0;
    uint8_t* kinds = 0;
    uint8_t* value_seen = 0;
    uint32_t* value_active_end = 0;
    uint32_t* use_intervals = 0;
    Tier1AllocationPieces pieces = { 0 };
    bool extended[BUSTER_TIER1_ALLOCATE_CLASSES] = { false, false };
    uint32_t event_count = 0;
    uint32_t fragment_capacity = 0;
    if (arena != 0 && result != 0 && tier1_allocation_input_valid(input))
    {
        error = TIER1_ALLOCATION_CAPACITY;
        fragment_capacity = input->count * 2 + (input->use_required != 0 ? input->use_count * 2 : 0);
        event_count = input->count * 2;
        if (input->use_required != 0)
        {
            for (uint32_t use = 0; use < input->use_count; use += 1)
            {
                event_count += input->use_required[use];
            }
        }
        allocation.interval_count = input->count;
        allocation.fragment_offsets = arena_push_array(arena, (uint64_t)input->count + 1, sizeof(uint32_t), 4);
        allocation.starts = arena_push_array(arena, fragment_capacity, sizeof(uint32_t), 4);
        allocation.ends = arena_push_array(arena, fragment_capacity, sizeof(uint32_t), 4);
        allocation.kinds = arena_push_array(arena, fragment_capacity, sizeof(uint8_t), 1);
        allocation.registers = arena_push_array(arena, fragment_capacity, sizeof(uint8_t), 1);
        allocation.value_registers = arena_push_array(arena, input->value_count, sizeof(uint8_t), 1);
        allocation.spill_ids = arena_push_array(arena, input->value_count, sizeof(uint32_t), 4);
        allocation.spill_values = arena_push_array(arena, input->value_count, sizeof(uint32_t), 4);
        retained = arena->length;
        buckets = arena_push_array(arena, (uint64_t)input->position_count + 2, sizeof(uint32_t), 4);
        events = arena_push_array(arena, event_count, sizeof(uint32_t), 4);
        allowed = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        split = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        regs = arena_push_array(arena, input->count, sizeof(uint8_t), 1);
        kinds = arena_push_array(arena, input->count, sizeof(uint8_t), 1);
        value_seen = arena_push_array(arena, input->value_count, sizeof(uint8_t), 1);
        value_active_end = arena_push_array(arena, input->value_count, sizeof(uint32_t), 4);
        if (allocation.fragment_offsets != 0 && allocation.starts != 0 && allocation.ends != 0 && allocation.kinds != 0 && allocation.registers != 0 && allocation.value_registers != 0 && allocation.spill_ids != 0 && allocation.spill_values != 0 && buckets != 0 && events != 0 && allowed != 0 && split != 0 && regs != 0 && kinds != 0 && value_seen != 0 && value_active_end != 0)
        {
            error = TIER1_ALLOCATION_OK;
        }
        if (error == TIER1_ALLOCATION_OK && vector)
        {
            parents = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
            next = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
            class_events = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(uint32_t), 4);
            deltas = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(int32_t), 4);
            pressure = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(int32_t), 4);
            if (parents == 0 || next == 0 || class_events == 0 || deltas == 0 || pressure == 0)
            {
                error = TIER1_ALLOCATION_CAPACITY;
            }
        }
        if (error == TIER1_ALLOCATION_OK && input->use_required != 0)
        {
            use_intervals = arena_push_array(arena, input->use_count, sizeof(uint32_t), 4);
            pieces.pieces = arena_push_array(arena, fragment_capacity, sizeof(Tier1AllocationPiece), _Alignof(Tier1AllocationPiece));
            pieces.heads = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
            pieces.tails = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
            if (use_intervals == 0 || pieces.pieces == 0 || pieces.heads == 0 || pieces.tails == 0)
            {
                error = TIER1_ALLOCATION_CAPACITY;
            }
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        memset(buckets, 0, ((uint64_t)input->position_count + 2) * sizeof(uint32_t));
        memset(value_seen, 0, input->value_count);
        memset(value_active_end, 0, (uint64_t)input->value_count * sizeof(uint32_t));
        memset(allocation.value_registers, UINT8_MAX, input->value_count);
        memset(allocation.spill_ids, 0xff, (uint64_t)input->value_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            buckets[input->starts[i] + 1] += 1;
            buckets[input->ends[i] + 1] += 1;
            allowed[i] = input->allowed_registers[i];
            split[i] = input->ends[i];
            regs[i] = UINT8_MAX;
            kinds[i] = BUSTER_TIER1_ALLOCATE_REGISTER;
            if (vector)
            {
                parents[i] = i;
            }
            if (input->use_required != 0)
            {
                pieces.heads[i] = UINT32_MAX;
                pieces.tails[i] = UINT32_MAX;
                for (uint32_t use = input->use_offsets[i]; use < input->use_offsets[i + 1]; use += 1)
                {
                    use_intervals[use] = i;
                    buckets[input->use_positions[use] + 1] += input->use_required[use];
                }
            }
        }
        for (uint32_t position = 1; position <= input->position_count + 1; position += 1)
        {
            buckets[position] += buckets[position - 1];
        }
        // End events precede starts at the same position, so adjacent half-open intervals can reuse registers.
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            events[buckets[input->ends[i]]] = i * 2;
            buckets[input->ends[i]] += 1;
        }
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            events[buckets[input->starts[i]]] = i * 2 + 1;
            buckets[input->starts[i]] += 1;
        }
        if (input->use_required != 0)
        {
            for (uint32_t use = 0; use < input->use_count; use += 1)
            {
                if (input->use_required[use] != 0)
                {
                    events[buckets[input->use_positions[use]]] = BUSTER_TIER1_ALLOCATE_USE_EVENT | use;
                    buckets[input->use_positions[use]] += 1;
                }
            }
        }
        for (uint32_t e = 0; error == TIER1_ALLOCATION_OK && e < event_count; e += 1)
        {
            if ((events[e] & BUSTER_TIER1_ALLOCATE_USE_EVENT) == 0 && (events[e] & 1) != 0)
            {
                const uint32_t i = events[e] >> 1;
                const uint32_t value = input->values[i];
                if (value_active_end[value] > input->starts[i])
                {
                    error = TIER1_ALLOCATION_INVALID;
                }
                if (value_seen[value] != 0 && value_seen[value] != input->classes[i] + 1)
                {
                    error = TIER1_ALLOCATION_INVALID;
                }
                value_seen[value] = input->classes[i] + 1;
                value_active_end[value] = input->ends[i];
            }
        }
        if (error == TIER1_ALLOCATION_OK && input->fixed.count != 0 && !tier1_allocation_filter_fixed(input, arena, vector, events, event_count, buckets, allowed))
        {
            error = TIER1_ALLOCATION_CAPACITY;
        }
        for (uint32_t class = 0; error == TIER1_ALLOCATION_OK && class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
        {
            bool regular = vector;
            uint32_t class_count = 0;
            uint32_t class_event_count = 0;
            int32_t live = 0;
            for (uint32_t i = 0; i < input->count; i += 1)
            {
                if (input->classes[i] == class)
                {
                    class_count += 1;
                    regular &= allowed[i] == input->registers[class];
                }
            }
            for (uint32_t e = 0; e < event_count; e += 1)
            {
                const uint32_t interval = events[e] >> 1;
                if ((events[e] & BUSTER_TIER1_ALLOCATE_USE_EVENT) == 0 && input->classes[interval] == class)
                {
                    const int32_t delta = (events[e] & 1) != 0 ? 1 : -1;
                    live += delta;
                    if ((uint32_t)live > allocation.peak_pressure[class])
                    {
                        allocation.peak_pressure[class] = (uint32_t)live;
                    }
                    if (vector)
                    {
                        class_events[class_event_count] = events[e];
                        deltas[class_event_count] = delta;
                    }
                    class_event_count += 1;
                }
            }
            regular &= allocation.peak_pressure[class] <= (uint32_t)__builtin_popcount(input->registers[class]);
            if (regular && class_count != 0)
            {
                tier1_allocation_pressure(deltas, class_event_count, pressure);
                tier1_allocation_color(input, class, class_events, pressure, class_event_count, parents, next, regs);
                allocation.vector_intervals += class_count;
            }
            else
            {
                allocation.scalar_intervals += class_count;
                bool assigned = false;
                if (input->use_required != 0)
                {
                    extended[class] = true;
                    assigned = tier1_allocate_scan_uses(input, class, events, event_count, use_intervals, allowed, regs, split, kinds, &pieces);
                }
                else
                {
                    assigned = tier1_allocate_scan(input, class, events, event_count, allowed, regs, split, kinds);
                }
                if (!assigned)
                {
                    error = TIER1_ALLOCATION_CONSTRAINT;
                }
            }
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        memset(value_seen, 0, input->value_count);
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            allocation.fragment_offsets[i] = allocation.fragment_count;
            const uint32_t value = input->values[i];
            uint8_t direct = UINT8_MAX;
            if (extended[input->classes[i]])
            {
                uint32_t piece_count = 0;
                for (uint32_t id = pieces.heads[i]; id != UINT32_MAX; id = pieces.pieces[id].next)
                {
                    const Tier1AllocationPiece piece = pieces.pieces[id];
                    const uint32_t fragment = allocation.fragment_count;
                    allocation.fragment_count += 1;
                    allocation.starts[fragment] = piece.start;
                    allocation.ends[fragment] = piece.end;
                    allocation.kinds[fragment] = piece.kind;
                    allocation.registers[fragment] = piece.reg;
                    if (piece.kind == BUSTER_TIER1_ALLOCATE_SPILL && allocation.spill_ids[value] == UINT32_MAX)
                    {
                        allocation.spill_ids[value] = allocation.spill_count;
                        allocation.spill_values[allocation.spill_count] = value;
                        allocation.spill_count += 1;
                    }
                    piece_count += 1;
                    direct = piece.kind == BUSTER_TIER1_ALLOCATE_REGISTER && piece_count == 1 ? piece.reg : UINT8_MAX;
                }
                allocation.splits += piece_count - 1;
            }
            else if (split[i] > input->starts[i])
            {
                const uint32_t fragment = allocation.fragment_count;
                allocation.fragment_count += 1;
                allocation.starts[fragment] = input->starts[i];
                allocation.ends[fragment] = split[i];
                allocation.kinds[fragment] = BUSTER_TIER1_ALLOCATE_REGISTER;
                allocation.registers[fragment] = regs[i];
            }
            if (!extended[input->classes[i]] && split[i] < input->ends[i])
            {
                const uint32_t fragment = allocation.fragment_count;
                allocation.fragment_count += 1;
                allocation.starts[fragment] = split[i];
                allocation.ends[fragment] = input->ends[i];
                allocation.kinds[fragment] = kinds[i];
                allocation.registers[fragment] = UINT8_MAX;
                allocation.splits += split[i] > input->starts[i] ? 1 : 0;
                if (kinds[i] == BUSTER_TIER1_ALLOCATE_SPILL && allocation.spill_ids[value] == UINT32_MAX)
                {
                    allocation.spill_ids[value] = allocation.spill_count;
                    allocation.spill_values[allocation.spill_count] = value;
                    allocation.spill_count += 1;
                }
            }
            if (!extended[input->classes[i]])
            {
                direct = split[i] == input->ends[i] ? regs[i] : UINT8_MAX;
            }
            if (value_seen[value] == 0)
            {
                allocation.value_registers[value] = direct;
                value_seen[value] = 1;
            }
            else if (allocation.value_registers[value] != direct)
            {
                allocation.value_registers[value] = UINT8_MAX;
            }
        }
        allocation.fragment_offsets[input->count] = allocation.fragment_count;
        allocation.retained_bytes = retained - saved;
        allocation.scratch_bytes = arena->length - retained;
        arena_pop_to(arena, retained);
        *result = allocation;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, saved);
    }
    return error;
}

static __attribute__((unused)) Tier1AllocationError tier1_allocate(const Tier1AllocationInput* input, Arena* arena, Tier1Allocation* result)
{
    return tier1_allocate_impl(input, arena, true, result);
}

static __attribute__((unused)) Tier1AllocationError tier1_allocate_scalar(const Tier1AllocationInput* input, Arena* arena, Tier1Allocation* result)
{
    return tier1_allocate_impl(input, arena, false, result);
}

static __attribute__((unused)) bool tier1_allocation_location(const Tier1AllocationInput* input, const Tier1Allocation* allocation, uint32_t interval, uint32_t position, Tier1AllocationLocation* result)
{
    bool found = false;
    if (interval < input->count && interval < allocation->interval_count)
    {
        for (uint32_t f = allocation->fragment_offsets[interval]; f < allocation->fragment_offsets[interval + 1]; f += 1)
        {
            if (position >= allocation->starts[f] && position < allocation->ends[f])
            {
                *result = (Tier1AllocationLocation){
                    .spill_id = allocation->spill_ids[input->values[interval]],
                    .kind = allocation->kinds[f],
                    .reg = allocation->registers[f],
                };
                found = true;
            }
        }
    }
    return found;
}

// Deliberately independent O(fragments^2) validity oracle for tests and qualification, not the production hot path.
static __attribute__((unused)) bool tier1_allocation_verify(const Tier1AllocationInput* input, const Tier1Allocation* allocation)
{
    bool valid = tier1_allocation_input_valid(input) && allocation != 0;
    if (valid)
    {
        valid = allocation->interval_count == input->count && allocation->fragment_count <= input->count * 2 + input->use_count * 2 && allocation->spill_count <= input->value_count;
        valid = valid && allocation->fragment_offsets != 0 && allocation->fragment_offsets[0] == 0 && allocation->fragment_offsets[input->count] == allocation->fragment_count;
        valid = valid && (allocation->fragment_count == 0 || (allocation->starts != 0 && allocation->ends != 0 && allocation->kinds != 0 && allocation->registers != 0));
        valid = valid && (input->value_count == 0 || (allocation->spill_ids != 0 && allocation->spill_values != 0 && allocation->value_registers != 0));
    }
    for (uint32_t i = 0; valid && i < input->count; i += 1)
    {
        const uint32_t first = allocation->fragment_offsets[i];
        const uint32_t end = allocation->fragment_offsets[i + 1];
        valid = first < end && end <= allocation->fragment_count;
        uint32_t cursor = input->starts[i];
        for (uint32_t f = first; valid && f < end; f += 1)
        {
            const uint8_t kind = allocation->kinds[f];
            valid = allocation->starts[f] == cursor && allocation->starts[f] < allocation->ends[f] && allocation->ends[f] <= input->ends[i] && kind <= BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
            cursor = allocation->ends[f];
            const uint8_t direct = allocation->value_registers[input->values[i]];
            valid = valid && (direct == UINT8_MAX || (kind == BUSTER_TIER1_ALLOCATE_REGISTER && direct == allocation->registers[f]));
            if (valid && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
            {
                const uint32_t reg = allocation->registers[f];
                valid = reg < 32 && ((input->allowed_registers[i] >> reg) & 1) != 0;
                for (uint32_t fixed = 0; valid && fixed < input->fixed.count; fixed += 1)
                {
                    valid = input->classes[i] != input->fixed.classes[fixed] || allocation->starts[f] >= input->fixed.ends[fixed] || allocation->ends[f] <= input->fixed.starts[fixed] || ((input->fixed.registers[fixed] >> reg) & 1) == 0;
                }
                for (uint32_t other = 0; valid && other < i; other += 1)
                {
                    for (uint32_t g = allocation->fragment_offsets[other]; valid && g < allocation->fragment_offsets[other + 1]; g += 1)
                    {
                        const bool overlap = allocation->starts[f] < allocation->ends[g] && allocation->ends[f] > allocation->starts[g];
                        valid = !overlap || (input->values[i] != input->values[other] && (input->classes[i] != input->classes[other] || allocation->kinds[g] != BUSTER_TIER1_ALLOCATE_REGISTER || reg != allocation->registers[g]));
                    }
                }
            }
            else if (valid)
            {
                valid = allocation->registers[f] == UINT8_MAX && (input->flags == 0 || (input->flags[i] & BUSTER_TIER1_ALLOCATE_REQUIRED) == 0);
                if (kind == BUSTER_TIER1_ALLOCATE_SPILL)
                {
                    const uint32_t slot = allocation->spill_ids[input->values[i]];
                    valid = valid && slot < allocation->spill_count && allocation->spill_values[slot] == input->values[i];
                }
                else
                {
                    valid = valid && input->rematerializable != 0 && input->rematerializable[i] != 0;
                }
            }
        }
        valid = valid && cursor == input->ends[i];
        if (input->use_required != 0)
        {
            for (uint32_t use = input->use_offsets[i]; valid && use < input->use_offsets[i + 1]; use += 1)
            {
                if (input->use_required[use] != 0)
                {
                    Tier1AllocationLocation location = { 0 };
                    valid = tier1_allocation_location(input, allocation, i, input->use_positions[use], &location) && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER;
                }
            }
        }
    }
    for (uint32_t s = 0; valid && s < allocation->spill_count; s += 1)
    {
        const uint32_t value = allocation->spill_values[s];
        valid = value < input->value_count && allocation->spill_ids[value] == s;
    }
    for (uint32_t value = 0; valid && value < input->value_count; value += 1)
    {
        const uint32_t slot = allocation->spill_ids[value];
        valid = slot == UINT32_MAX || (slot < allocation->spill_count && allocation->spill_values[slot] == value);
    }
    return valid;
}

// Location ids are opaque to this routine: registers and stack slots can occupy separate caller-defined ranges.
// Moves are same-width, same-class, non-aliasing location assignments; each destination is unique. The scratch id
// must not occur in the input. A caller that cannot emit memory-to-memory moves reserves another machine scratch.
typedef struct Tier1ParallelMoves Tier1ParallelMoves;
struct Tier1ParallelMoves
{
    const uint32_t* sources;
    const uint32_t* destinations;
    uint32_t count;
    uint32_t scratch;
};

typedef struct Tier1ResolvedMoves Tier1ResolvedMoves;
struct Tier1ResolvedMoves
{
    uint32_t* sources;
    uint32_t* destinations;
    uint32_t count;
};

#define BUSTER_TIER1_MOVE_SMALL_COUNT 8

// Independent small-set oracle, also used for the bounded argument-register shuffles (at most six moves).
static __attribute__((unused)) Tier1AllocationError tier1_resolve_moves_scalar(const Tier1ParallelMoves* input, Arena* arena, Tier1ResolvedMoves* result)
{
    Tier1AllocationError error = TIER1_ALLOCATION_INVALID;
    const uint64_t saved = arena != 0 ? arena->length : 0;
    Tier1ResolvedMoves output = { 0 };
    uint32_t* sources = 0;
    uint8_t* pending = 0;
    uint64_t retained = saved;
    if (input != 0 && arena != 0 && result != 0 && input->count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && (input->count == 0 || (input->sources != 0 && input->destinations != 0)))
    {
        error = TIER1_ALLOCATION_OK;
        for (uint32_t i = 0; error == TIER1_ALLOCATION_OK && i < input->count; i += 1)
        {
            if (input->sources[i] == input->scratch || input->destinations[i] == input->scratch)
            {
                error = TIER1_ALLOCATION_INVALID;
            }
            for (uint32_t j = 0; error == TIER1_ALLOCATION_OK && j < i; j += 1)
            {
                if (input->destinations[i] == input->destinations[j])
                {
                    error = TIER1_ALLOCATION_INVALID;
                }
            }
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        output.sources = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(uint32_t), 4);
        output.destinations = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(uint32_t), 4);
        retained = arena->length;
        sources = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        pending = arena_push_array(arena, input->count, sizeof(uint8_t), 1);
        if (output.sources == 0 || output.destinations == 0 || sources == 0 || pending == 0)
        {
            error = TIER1_ALLOCATION_CAPACITY;
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        uint32_t remaining = 0;
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            sources[i] = input->sources[i];
            pending[i] = input->sources[i] != input->destinations[i] ? 1 : 0;
            remaining += pending[i];
        }
        while (remaining != 0)
        {
            uint32_t chosen = UINT32_MAX;
            uint32_t first = UINT32_MAX;
            for (uint32_t i = 0; chosen == UINT32_MAX && i < input->count; i += 1)
            {
                if (pending[i] != 0)
                {
                    first = first == UINT32_MAX ? i : first;
                    bool used = false;
                    for (uint32_t j = 0; j < input->count; j += 1)
                    {
                        used |= pending[j] != 0 && sources[j] == input->destinations[i];
                    }
                    if (!used)
                    {
                        chosen = i;
                    }
                }
            }
            if (chosen != UINT32_MAX)
            {
                output.sources[output.count] = sources[chosen];
                output.destinations[output.count] = input->destinations[chosen];
                output.count += 1;
                pending[chosen] = 0;
                remaining -= 1;
            }
            else
            {
                const uint32_t preserved = input->destinations[first];
                output.sources[output.count] = preserved;
                output.destinations[output.count] = input->scratch;
                output.count += 1;
                for (uint32_t j = 0; j < input->count; j += 1)
                {
                    if (pending[j] != 0 && sources[j] == preserved)
                    {
                        sources[j] = input->scratch;
                    }
                }
            }
        }
        arena_pop_to(arena, retained);
        *result = output;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, saved);
    }
    return error;
}

// Bottom-up merge sort of (opaque destination, original index). No location-sized table, hash collision worst
// case, callback or input-dependent recursion. Each pass streams both runs and writes each key exactly once.
static uint64_t* tier1_move_sort(uint64_t* keys, uint64_t* temporary, uint32_t count)
{
    for (uint32_t width = 1; width < count; width *= 2)
    {
        for (uint32_t begin = 0; begin < count; begin += width * 2)
        {
            const uint32_t middle = begin + width < count ? begin + width : count;
            const uint32_t end = middle + width < count ? middle + width : count;
            uint32_t left = begin;
            uint32_t right = middle;
            for (uint32_t out = begin; out < end; out += 1)
            {
                const bool take_left = right == end || (left < middle && keys[left] < keys[right]);
                temporary[out] = take_left ? keys[left++] : keys[right++];
            }
        }
        uint64_t* swap = keys;
        keys = temporary;
        temporary = swap;
    }
    return keys;
}

// Only newly ready moves enter the minimum heap; removing a move updates the count of its one source.
static void tier1_move_ready_push(uint32_t* ready, uint32_t* count, uint32_t move)
{
    uint32_t position = *count;
    *count += 1;
    while (position != 0 && ready[(position - 1) / 2] > move)
    {
        ready[position] = ready[(position - 1) / 2];
        position = (position - 1) / 2;
    }
    ready[position] = move;
}

static uint32_t tier1_move_ready_pop(uint32_t* ready, uint32_t* count)
{
    const uint32_t result = ready[0];
    *count -= 1;
    const uint32_t last = ready[*count];
    uint32_t position = 0;
    bool placed = false;
    while (!placed && position * 2 + 1 < *count)
    {
        uint32_t child = position * 2 + 1;
        if (child + 1 < *count && ready[child + 1] < ready[child])
        {
            child += 1;
        }
        placed = last <= ready[child];
        if (!placed)
        {
            ready[position] = ready[child];
            position = child;
        }
    }
    ready[position] = last;
    return result;
}

static __attribute__((unused)) Tier1AllocationError tier1_resolve_moves_indexed(const Tier1ParallelMoves* input, Arena* arena, Tier1ResolvedMoves* result, bool vector)
{
    Tier1AllocationError error = TIER1_ALLOCATION_INVALID;
    const uint64_t saved = arena != 0 ? arena->length : 0;
    Tier1ResolvedMoves output = { 0 };
    uint64_t* keys = 0;
    uint64_t* temporary = 0;
    uint32_t* dependencies = 0;
    uint32_t* uses = 0;
    uint32_t* ready = 0;
    uint64_t retained = saved;
    if (input != 0 && arena != 0 && result != 0 && input->count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && (input->count == 0 || (input->sources != 0 && input->destinations != 0)))
    {
        error = TIER1_ALLOCATION_OK;
        output.sources = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(uint32_t), 4);
        output.destinations = arena_push_array(arena, (uint64_t)input->count * 2, sizeof(uint32_t), 4);
        retained = arena->length;
        keys = arena_push_array(arena, input->count, sizeof(uint64_t), 8);
        temporary = arena_push_array(arena, input->count, sizeof(uint64_t), 8);
        dependencies = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        uses = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        ready = arena_push_array(arena, input->count, sizeof(uint32_t), 4);
        if (output.sources == 0 || output.destinations == 0 || keys == 0 || temporary == 0 || dependencies == 0 || uses == 0 || ready == 0)
        {
            error = TIER1_ALLOCATION_CAPACITY;
        }
    }
    if (error == TIER1_ALLOCATION_OK && vector)
    {
        const __m256i scratch = _mm256_set1_epi32((int)input->scratch);
        const __m512i indices = _mm512_setr_epi64(0, 1, 2, 3, 4, 5, 6, 7);
        for (uint32_t i = 0; error == TIER1_ALLOCATION_OK && i < input->count; i += 8)
        {
            const uint32_t left = input->count - i;
            const __mmask8 mask = left >= 8 ? 0xff : (__mmask8)((1u << left) - 1);
            const __m256i sources = _mm256_maskz_loadu_epi32(mask, input->sources + i);
            const __m256i destinations = _mm256_maskz_loadu_epi32(mask, input->destinations + i);
            const __mmask8 invalid = _mm256_cmpeq_epi32_mask(sources, scratch) | _mm256_cmpeq_epi32_mask(destinations, scratch);
            if ((invalid & mask) != 0)
            {
                error = TIER1_ALLOCATION_INVALID;
            }
            const __m256i pending = _mm256_maskz_set1_epi32(_mm256_cmpeq_epi32_mask(sources, destinations), -1);
            _mm256_mask_storeu_epi32(uses + i, mask, pending);
            const __m512i locations = _mm512_slli_epi64(_mm512_cvtepu32_epi64(destinations), 32);
            const __m512i positions = _mm512_add_epi64(indices, _mm512_set1_epi64(i));
            _mm512_mask_storeu_epi64(keys + i, mask, _mm512_or_si512(locations, positions));
        }
    }
    else if (error == TIER1_ALLOCATION_OK)
    {
        for (uint32_t i = 0; error == TIER1_ALLOCATION_OK && i < input->count; i += 1)
        {
            if (input->sources[i] == input->scratch || input->destinations[i] == input->scratch)
            {
                error = TIER1_ALLOCATION_INVALID;
            }
            uses[i] = input->sources[i] == input->destinations[i] ? UINT32_MAX : 0;
            keys[i] = ((uint64_t)input->destinations[i] << 32) | i;
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        keys = tier1_move_sort(keys, temporary, input->count);
        for (uint32_t i = 1; error == TIER1_ALLOCATION_OK && i < input->count; i += 1)
        {
            if ((keys[i - 1] >> 32) == (keys[i] >> 32))
            {
                error = TIER1_ALLOCATION_INVALID;
            }
        }
    }
    if (error == TIER1_ALLOCATION_OK)
    {
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            uint32_t begin = 0;
            uint32_t end = input->count;
            const uint32_t source = input->sources[i];
            while (begin < end)
            {
                const uint32_t middle = begin + (end - begin) / 2;
                if ((keys[middle] >> 32) < source)
                {
                    begin = middle + 1;
                }
                else
                {
                    end = middle;
                }
            }
            uint32_t dependency = begin < input->count && (keys[begin] >> 32) == source ? (uint32_t)keys[begin] : UINT32_MAX;
            dependency = dependency != UINT32_MAX && uses[dependency] != UINT32_MAX ? dependency : UINT32_MAX;
            dependencies[i] = dependency;
            if (uses[i] != UINT32_MAX && dependency != UINT32_MAX)
            {
                uses[dependency] += 1;
            }
        }
        uint32_t ready_count = 0;
        uint32_t remaining = 0;
        for (uint32_t i = 0; i < input->count; i += 1)
        {
            remaining += uses[i] != UINT32_MAX;
            if (uses[i] == 0)
            {
                // Increasing input indices already form a minimum heap.
                ready[ready_count++] = i;
            }
        }
        uint32_t first = 0;
        uint32_t preserved = UINT32_MAX;
        while (remaining != 0)
        {
            if (ready_count == 0)
            {
                while (uses[first] == UINT32_MAX)
                {
                    first += 1;
                }
                // Every residual destination is used, and each move has one source. Thus every residual use count
                // is exactly one: disjoint cycles, with fan-out already drained. Only one consumer needs scratch.
                preserved = first;
                output.sources[output.count] = input->destinations[first];
                output.destinations[output.count] = input->scratch;
                output.count += 1;
                uses[first] = 0;
                tier1_move_ready_push(ready, &ready_count, first);
            }
            const uint32_t chosen = tier1_move_ready_pop(ready, &ready_count);
            const uint32_t dependency = dependencies[chosen];
            const bool from_scratch = dependency != UINT32_MAX && dependency == preserved;
            output.sources[output.count] = from_scratch ? input->scratch : input->sources[chosen];
            output.destinations[output.count] = input->destinations[chosen];
            output.count += 1;
            uses[chosen] = UINT32_MAX;
            remaining -= 1;
            if (from_scratch)
            {
                preserved = UINT32_MAX;
            }
            else if (dependency != UINT32_MAX)
            {
                uses[dependency] -= 1;
                if (uses[dependency] == 0)
                {
                    tier1_move_ready_push(ready, &ready_count, dependency);
                }
            }
        }
        arena_pop_to(arena, retained);
        *result = output;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, saved);
    }
    return error;
}

static __attribute__((unused)) Tier1AllocationError tier1_resolve_moves(const Tier1ParallelMoves* input, Arena* arena, Tier1ResolvedMoves* result)
{
    const bool small = input != 0 && input->count <= BUSTER_TIER1_MOVE_SMALL_COUNT;
    const Tier1AllocationError error = small ? tier1_resolve_moves_scalar(input, arena, result) : tier1_resolve_moves_indexed(input, arena, result, true);
    return error;
}
