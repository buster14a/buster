// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1 optimization (#139). The IR owner supplies ir.c; this file owns transfer functions and transformations,
// never another IR representation. Integer evaluation uses unsigned bits and refuses a faulting division or an
// out-of-range shift. Unknown analysis state and an IR undef are different: undef must enter as overdefined.
// The 16-item fold boundary uses bounded columns; it has no padding, alignment or inactive-lane read contract.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_IR_OPTIMIZE_BATCH 16
#define BUSTER_IR_OPTIMIZE_DEFAULT_SWEEPS 64
#define BUSTER_IR_OPTIMIZE_SCCP 1u
#define BUSTER_IR_OPTIMIZE_DCE 2u
#define BUSTER_IR_OPTIMIZE_CFG 4u

typedef struct IrOptimizeOptions IrOptimizeOptions;
struct IrOptimizeOptions
{
    uint32_t passes;
    uint32_t sweep_limit;
    bool scalar;
};

typedef struct IrOptimizeResult IrOptimizeResult;
struct IrOptimizeResult
{
    IrFunction function;
    uint64_t sccp_visits;
    uint64_t sccp_sweeps;
    uint64_t dce_visits;
    uint64_t dce_sweeps;
    uint64_t scratch_bytes;
    uint32_t folded;
    uint32_t removed_instructions;
    uint32_t removed_blocks;
    bool worklist_fallback;
};

typedef enum IrOptimizeState
{
    IROPT_UNKNOWN,
    IROPT_CONSTANT,
    IROPT_OVERDEFINED,
} IrOptimizeState;

typedef struct IrOptimizeWork IrOptimizeWork;
struct IrOptimizeWork
{
    const IrFunction* function;
    IrCfg cfg;
    IrUses uses;
    Dominators dominators;
    uint8_t* states;
    IrConstant* values;
    uint8_t* blocks;
    uint8_t* edges;
    uint8_t* live;
    uint8_t* queued;
    uint32_t* queue;
    uint32_t head;
    uint32_t tail;
    uint32_t queued_count;
    IrOptimizeResult statistics;
};

static __attribute__((unused)) IrOptimizeOptions ir_optimize_options(void)
{
    const IrOptimizeOptions result = {
        .passes = BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG,
        .sweep_limit = BUSTER_IR_OPTIMIZE_DEFAULT_SWEEPS,
    };
    return result;
}

static __attribute__((unused)) IrCheck ir_optimize(const IrFunction* function, Arena* output, Arena* scratch, IrOptimizeOptions options, IrOptimizeResult* result);

// Transfer-function classes, internal to this module. These are not IR opcodes.
typedef enum IrOptimizeFoldOp
{
    IROPT_FOLD_NONE,
    IROPT_FOLD_ADD,
    IROPT_FOLD_SUB,
    IROPT_FOLD_MUL,
    IROPT_FOLD_AND,
    IROPT_FOLD_OR,
    IROPT_FOLD_XOR,
    IROPT_FOLD_SHL,
    IROPT_FOLD_LSHR,
    IROPT_FOLD_ASHR,
    IROPT_FOLD_UDIV,
    IROPT_FOLD_UREM,
    IROPT_FOLD_SDIV,
    IROPT_FOLD_SREM,
    IROPT_FOLD_EQ,
    IROPT_FOLD_NE,
    IROPT_FOLD_ULT,
    IROPT_FOLD_ULE,
    IROPT_FOLD_SLT,
    IROPT_FOLD_SLE,
} IrOptimizeFoldOp;

typedef struct IrOptimizeFoldBatch IrOptimizeFoldBatch;
struct IrOptimizeFoldBatch
{
    uint32_t count;
    const uint32_t* operation;
    const uint32_t* width;
    const uint64_t* left;
    const uint64_t* right;
    uint64_t* result;
};

static uint64_t ir_optimize_width_mask(uint32_t width)
{
    const uint64_t result = UINT64_MAX >> (64 - width);
    return result;
}

// All operations take normalized unsigned bit patterns. Signed comparisons use the sign-bit-biased ordering;
// signed division works on magnitudes, so neither signed overflow nor implementation-defined casts enter folding.
static __attribute__((unused)) bool ir_optimize_fold_one(uint32_t operation, uint32_t width, uint64_t left, uint64_t right, uint64_t* output)
{
    bool folded = width != 0 && width <= 64;
    uint64_t result = 0;
    if (folded)
    {
        const uint64_t mask = ir_optimize_width_mask(width);
        const uint64_t sign = UINT64_C(1) << (width - 1);
        left &= mask;
        if (operation < IROPT_FOLD_SHL || operation > IROPT_FOLD_ASHR)
        {
            right &= mask;
        }
        switch (operation)
        {
            case IROPT_FOLD_ADD: result = left + right; break;
            case IROPT_FOLD_SUB: result = left - right; break;
            case IROPT_FOLD_MUL: result = left * right; break;
            case IROPT_FOLD_AND: result = left & right; break;
            case IROPT_FOLD_OR: result = left | right; break;
            case IROPT_FOLD_XOR: result = left ^ right; break;
            case IROPT_FOLD_EQ: result = left == right; break;
            case IROPT_FOLD_NE: result = left != right; break;
            case IROPT_FOLD_ULT: result = left < right; break;
            case IROPT_FOLD_ULE: result = left <= right; break;
            case IROPT_FOLD_SLT: result = (left ^ sign) < (right ^ sign); break;
            case IROPT_FOLD_SLE: result = (left ^ sign) <= (right ^ sign); break;
            case IROPT_FOLD_SHL:
            case IROPT_FOLD_LSHR:
            case IROPT_FOLD_ASHR:
            {
                folded = right < width;
                if (folded)
                {
                    if (operation == IROPT_FOLD_SHL)
                    {
                        result = left << right;
                    }
                    else
                    {
                        result = left >> right;
                        if (operation == IROPT_FOLD_ASHR && (left & sign) != 0 && right != 0)
                        {
                            result |= mask ^ (mask >> right);
                        }
                    }
                }
            } break;
            case IROPT_FOLD_UDIV:
            case IROPT_FOLD_UREM:
            {
                folded = right != 0;
                if (folded)
                {
                    result = operation == IROPT_FOLD_UDIV ? left / right : left % right;
                }
            } break;
            case IROPT_FOLD_SDIV:
            case IROPT_FOLD_SREM:
            {
                folded = right != 0 && !(left == sign && right == mask);
                if (folded)
                {
                    const bool left_negative = (left & sign) != 0;
                    const bool right_negative = (right & sign) != 0;
                    const uint64_t left_magnitude = left_negative ? (-left & mask) : left;
                    const uint64_t right_magnitude = right_negative ? (-right & mask) : right;
                    result = operation == IROPT_FOLD_SDIV ? left_magnitude / right_magnitude : left_magnitude % right_magnitude;
                    const bool negative = operation == IROPT_FOLD_SDIV ? left_negative != right_negative : left_negative;
                    if (negative)
                    {
                        result = -result;
                    }
                }
            } break;
            default: folded = false; break;
        }
        result &= mask;
    }
    if (folded)
    {
        *output = result;
    }
    return folded;
}

// A returned bit authorizes its result slot. Inactive or refused slots are left unchanged by both paths.
static __attribute__((unused)) uint16_t ir_optimize_fold_scalar(IrOptimizeFoldBatch batch, uint16_t active)
{
    uint16_t folded = 0;
    if (batch.count <= BUSTER_IR_OPTIMIZE_BATCH)
    {
        for (uint32_t i = 0; i < batch.count; i += 1)
        {
            if ((active & (UINT32_C(1) << i)) != 0 && ir_optimize_fold_one(batch.operation[i], batch.width[i], batch.left[i], batch.right[i], &batch.result[i]))
            {
                folded |= UINT32_C(1) << i;
            }
        }
    }
    return folded;
}

static __attribute__((unused)) uint16_t ir_optimize_fold_vector(IrOptimizeFoldBatch batch, uint16_t active)
{
#if !BUSTER_HOST_VECTOR
    return ir_optimize_fold_scalar(batch, active);
#else
    uint16_t folded = 0;
    if (batch.count <= BUSTER_IR_OPTIMIZE_BATCH)
    {
        for (uint32_t first = 0; first < batch.count; first += 8)
        {
            const uint32_t count = batch.count - first < 8 ? batch.count - first : 8;
            const __mmask8 lanes = ((UINT32_C(1) << count) - 1) & (active >> first);
            const __m256i operations = _mm256_maskz_loadu_epi32(lanes, batch.operation + first);
            const __m256i widths = _mm256_maskz_loadu_epi32(lanes, batch.width + first);
            const __mmask8 valid = lanes & _mm256_cmpgt_epi32_mask(widths, _mm256_setzero_si256()) & _mm256_cmple_epu32_mask(widths, _mm256_set1_epi32(64));
            const __m512i shift = _mm512_sub_epi64(_mm512_set1_epi64(64), _mm512_cvtepu32_epi64(widths));
            const __m512i masks = _mm512_srlv_epi64(_mm512_set1_epi64(-1), shift);
            const __m512i left = _mm512_and_si512(_mm512_maskz_loadu_epi64(valid, batch.left + first), masks);
            const __m512i raw_right = _mm512_maskz_loadu_epi64(valid, batch.right + first);
            const __mmask8 shifts = _mm256_cmpge_epu32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_SHL)) & _mm256_cmple_epu32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_ASHR));
            const __m512i right = _mm512_mask_blend_epi64(shifts, _mm512_and_si512(raw_right, masks), raw_right);
            __m512i result = _mm512_setzero_si512();
            __mmask8 done = 0;
            __mmask8 selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_ADD));
            if (selected != 0)
            {
                result = _mm512_mask_add_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_SUB));
            if (selected != 0)
            {
                result = _mm512_mask_sub_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_MUL));
            if (selected != 0)
            {
                result = _mm512_mask_mullo_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_AND));
            if (selected != 0)
            {
                result = _mm512_mask_and_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_OR));
            if (selected != 0)
            {
                result = _mm512_mask_or_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_XOR));
            if (selected != 0)
            {
                result = _mm512_mask_xor_epi64(result, selected, left, right);
                done |= selected;
            }
            const __mmask8 shift_valid = valid & _mm512_cmplt_epu64_mask(right, _mm512_cvtepu32_epi64(widths));
            selected = shift_valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_SHL));
            if (selected != 0)
            {
                result = _mm512_mask_sllv_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = shift_valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_LSHR));
            if (selected != 0)
            {
                result = _mm512_mask_srlv_epi64(result, selected, left, right);
                done |= selected;
            }
            selected = shift_valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_ASHR));
            if (selected != 0)
            {
                const __m512i signed_left = _mm512_srav_epi64(_mm512_sllv_epi64(left, shift), shift);
                result = _mm512_mask_srav_epi64(result, selected, signed_left, right);
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_EQ));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmpeq_epi64_mask(left, right), 1));
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_NE));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmpneq_epi64_mask(left, right), 1));
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_ULT));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmplt_epu64_mask(left, right), 1));
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_ULE));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmple_epu64_mask(left, right), 1));
                done |= selected;
            }
            const __m512i sign = _mm512_sub_epi64(masks, _mm512_srli_epi64(masks, 1));
            const __m512i biased_left = _mm512_xor_si512(left, sign);
            const __m512i biased_right = _mm512_xor_si512(right, sign);
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_SLT));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmplt_epu64_mask(biased_left, biased_right), 1));
                done |= selected;
            }
            selected = valid & _mm256_cmpeq_epi32_mask(operations, _mm256_set1_epi32(IROPT_FOLD_SLE));
            if (selected != 0)
            {
                result = _mm512_mask_mov_epi64(result, selected, _mm512_maskz_set1_epi64(_mm512_cmple_epu64_mask(biased_left, biased_right), 1));
                done |= selected;
            }
            _mm512_mask_storeu_epi64(batch.result + first, done, _mm512_and_si512(result, masks));
            folded |= (uint16_t)done << first;
            uint32_t remaining = lanes & ~done;
            while (remaining != 0)
            {
                const uint32_t i = first + (uint32_t)__builtin_ctz(remaining);
                if (ir_optimize_fold_one(batch.operation[i], batch.width[i], batch.left[i], batch.right[i], &batch.result[i]))
                {
                    folded |= UINT32_C(1) << i;
                }
                remaining &= remaining - 1;
            }
        }
    }
    return folded;
#endif
}

// Up to 64 block ids fit four registers. A masked tail load authorizes only the logical source entries, and every
// index has already been checked. Larger graphs use scalar indexed loads rather than Zen 5's expensive gathers.
static __m512i ir_optimize_lookup_blocks(const uint32_t* values, const uint32_t* indices, uint32_t first, uint32_t count, uint32_t total)
{
    const __mmask16 active = (UINT32_C(1) << count) - 1;
    const __m512i index = _mm512_maskz_loadu_epi32(active, indices + first);
    __m512i result;
    if (total <= 64)
    {
        __m512i tables[4];
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const uint32_t begin = group * 16;
            const uint32_t amount = begin < total ? (total - begin < 16 ? total - begin : 16) : 0;
            if (amount != 0)
            {
                tables[group] = _mm512_maskz_loadu_epi32((UINT32_C(1) << amount) - 1, values + begin);
            }
            else
            {
                tables[group] = _mm512_setzero_si512();
            }
        }
        const __m512i low = _mm512_permutex2var_epi32(tables[0], index, tables[1]);
        const __m512i high = _mm512_permutex2var_epi32(tables[2], index, tables[3]);
        result = _mm512_mask_blend_epi32(_mm512_test_epi32_mask(index, _mm512_set1_epi32(32)), low, high);
    }
    else
    {
        uint32_t gathered[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            gathered[i] = values[indices[first + i]];
        }
        result = _mm512_loadu_si512(gathered);
    }
    return result;
}

// direct[b] is b for an observable block; otherwise the sole successor of a transparent unconditional jump.
// The caller must protect phi-edge identity before classifying a block as transparent. A cycle of transparent
// jumps resolves to its smallest block id, where the consumer retains a self-loop. This preserves nontermination.
// Both implementations write exactly the same targets. Failure leaves targets and scratch.length unchanged.
static __attribute__((unused)) bool ir_optimize_thread_targets(Arena* scratch, const uint32_t* direct, uint32_t count, uint32_t* targets, bool scalar)
{
    (void)scalar;

    const uint64_t saved = scratch->length;
    bool valid = count == 0 || (direct != 0 && targets != 0);
    for (uint32_t block = 0; valid && block < count; block += 1)
    {
        valid = direct[block] < count;
    }
    uint32_t* current = 0;
    uint32_t* minimum = 0;
    uint32_t* next = 0;
    uint32_t* next_minimum = 0;
    if (valid && count != 0)
    {
        current = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        minimum = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        next = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        next_minimum = arena_push_array(scratch, count, sizeof(uint32_t), _Alignof(uint32_t));
        valid = current != 0 && minimum != 0 && next != 0 && next_minimum != 0;
    }
    if (valid && count != 0 && BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t block = 0; block < count; block += 1)
        {
            current[block] = UINT32_MAX;
            minimum[block] = UINT32_MAX;
        }
        for (uint32_t block = 0; block < count; block += 1)
        {
            uint32_t node = block;
            uint32_t length = 0;
            while (current[node] == UINT32_MAX && minimum[node] != block)
            {
                minimum[node] = block;
                next_minimum[node] = length;
                next[length++] = node;
                node = direct[node];
            }
            uint32_t representative = current[node];
            if (representative == UINT32_MAX)
            {
                representative = node;
                for (uint32_t i = next_minimum[node]; i < length; i += 1)
                {
                    if (next[i] < representative)
                    {
                        representative = next[i];
                    }
                }
            }
            for (uint32_t i = 0; i < length; i += 1)
            {
                current[next[i]] = representative;
            }
        }
    }
    else if (valid && count != 0)
    {
        memcpy(current, direct, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t block = 0; block < count; block += 1)
        {
            minimum[block] = block;
        }
        for (uint64_t covered = 1; covered < count; covered *= 2)
        {
            for (uint64_t first = 0; first < count; first += BUSTER_IR_OPTIMIZE_BATCH)
            {
                const uint32_t amount = count - first < BUSTER_IR_OPTIMIZE_BATCH ? count - first : BUSTER_IR_OPTIMIZE_BATCH;
                const __mmask16 active = (UINT32_C(1) << amount) - 1;
                const __m512i target = ir_optimize_lookup_blocks(current, current, first, amount, count);
                const __m512i path_minimum = ir_optimize_lookup_blocks(minimum, current, first, amount, count);
                const __m512i old_minimum = _mm512_maskz_loadu_epi32(active, minimum + first);
                _mm512_mask_storeu_epi32(next + first, active, target);
                _mm512_mask_storeu_epi32(next_minimum + first, active, _mm512_min_epu32(old_minimum, path_minimum));
            }
            uint32_t* swap = current;
            current = next;
            next = swap;
            swap = minimum;
            minimum = next_minimum;
            next_minimum = swap;
        }
        // After at least count hops, current points inside the terminal cycle, or at an observable fixed point.
        // The propagated minima at those nodes contain the cycle only, never a smaller id on an incoming tail.
        for (uint32_t block = 0; block < count; block += 1)
        {
            next[block] = minimum[current[block]];
        }
        current = next;
    }
    if (valid && count != 0)
    {
        memcpy(targets, current, (uint64_t)count * sizeof(uint32_t));
    }
    arena_pop_to(scratch, saved);
    return valid;
}

static bool ir_optimize_equal(IrConstant a, IrConstant b)
{
    const bool result = a.low == b.low && a.high == b.high;
    return result;
}

static uint32_t ir_optimize_fold_operation(const IrFunction* f, uint32_t i, bool* swap)
{
    static const uint8_t operations[64] = {
        [IR_ADD] = IROPT_FOLD_ADD,
        [IR_SUB] = IROPT_FOLD_SUB,
        [IR_MUL] = IROPT_FOLD_MUL,
        [IR_AND] = IROPT_FOLD_AND,
        [IR_OR] = IROPT_FOLD_OR,
        [IR_XOR] = IROPT_FOLD_XOR,
        [IR_SHL] = IROPT_FOLD_SHL,
        [IR_LSHR] = IROPT_FOLD_LSHR,
        [IR_ASHR] = IROPT_FOLD_ASHR,
        [IR_UDIV] = IROPT_FOLD_UDIV,
        [IR_SDIV] = IROPT_FOLD_SDIV,
        [IR_UREM] = IROPT_FOLD_UREM,
        [IR_SREM] = IROPT_FOLD_SREM,
    };
    uint32_t operation = operations[f->op[i]];
    *swap = false;
    if (f->op[i] == IR_ICMP)
    {
        static const uint8_t predicates[IR_PREDICATE_COUNT] = {
            IROPT_FOLD_EQ,
            IROPT_FOLD_NE,
            IROPT_FOLD_SLT,
            IROPT_FOLD_SLE,
            IROPT_FOLD_SLT,
            IROPT_FOLD_SLE,
            IROPT_FOLD_ULT,
            IROPT_FOLD_ULE,
            IROPT_FOLD_ULT,
            IROPT_FOLD_ULE,
        };
        operation = predicates[f->c[i]];
        *swap = f->c[i] == IR_GT || f->c[i] == IR_GE || f->c[i] == IR_UGT || f->c[i] == IR_UGE;
    }
    return operation;
}

static unsigned __int128 ir_optimize_bits(IrConstant value)
{
    const unsigned __int128 result = ((unsigned __int128)value.high << 64) | value.low;
    return result;
}

static IrConstant ir_optimize_constant(unsigned __int128 value, uint32_t width)
{
    if (width < 128)
    {
        value &= ((unsigned __int128)1 << width) - 1;
    }
    const IrConstant result = {
        .low = (uint64_t)value,
        .high = (uint64_t)(value >> 64),
    };
    return result;
}

static bool ir_optimize_fold_wide(uint32_t operation, IrConstant a, IrConstant b, IrConstant* output)
{
    const unsigned __int128 left = ir_optimize_bits(a);
    const unsigned __int128 right = ir_optimize_bits(b);
    const unsigned __int128 sign = (unsigned __int128)1 << 127;
    unsigned __int128 value = 0;
    bool folded = true;
    switch (operation)
    {
        case IROPT_FOLD_ADD: value = left + right; break;
        case IROPT_FOLD_SUB: value = left - right; break;
        case IROPT_FOLD_MUL: value = left * right; break;
        case IROPT_FOLD_AND: value = left & right; break;
        case IROPT_FOLD_OR: value = left | right; break;
        case IROPT_FOLD_XOR: value = left ^ right; break;
        case IROPT_FOLD_EQ: value = left == right; break;
        case IROPT_FOLD_NE: value = left != right; break;
        case IROPT_FOLD_ULT: value = left < right; break;
        case IROPT_FOLD_ULE: value = left <= right; break;
        case IROPT_FOLD_SLT: value = (left ^ sign) < (right ^ sign); break;
        case IROPT_FOLD_SLE: value = (left ^ sign) <= (right ^ sign); break;
        case IROPT_FOLD_SHL:
        case IROPT_FOLD_LSHR:
        case IROPT_FOLD_ASHR:
        {
            folded = right < 128;
            if (folded)
            {
                if (operation == IROPT_FOLD_SHL)
                {
                    value = left << (uint32_t)right;
                }
                else
                {
                    value = left >> (uint32_t)right;
                    if (operation == IROPT_FOLD_ASHR && (left & sign) != 0 && right != 0)
                    {
                        value |= ~(unsigned __int128)0 << (128 - (uint32_t)right);
                    }
                }
            }
        } break;
        case IROPT_FOLD_UDIV:
        case IROPT_FOLD_UREM:
        {
            folded = right != 0;
            if (folded)
            {
                value = operation == IROPT_FOLD_UDIV ? left / right : left % right;
            }
        } break;
        case IROPT_FOLD_SDIV:
        case IROPT_FOLD_SREM:
        {
            folded = right != 0 && !(left == sign && right == ~(unsigned __int128)0);
            if (folded)
            {
                const bool negative_left = (left & sign) != 0;
                const bool negative_right = (right & sign) != 0;
                const unsigned __int128 magnitude_left = negative_left ? -left : left;
                const unsigned __int128 magnitude_right = negative_right ? -right : right;
                value = operation == IROPT_FOLD_SDIV ? magnitude_left / magnitude_right : magnitude_left % magnitude_right;
                if (operation == IROPT_FOLD_SDIV ? negative_left != negative_right : negative_left)
                {
                    value = -value;
                }
            }
        } break;
        default: folded = false; break;
    }
    if (folded)
    {
        *output = ir_optimize_constant(value, 128);
    }
    return folded;
}

static uint32_t ir_optimize_edge(const IrCfg* cfg, uint32_t from, uint32_t to)
{
    uint32_t result = BUSTER_IR_NONE;
    for (uint32_t edge = cfg->successor_offsets[from]; edge < cfg->successor_offsets[(uint64_t)from + 1]; edge += 1)
    {
        if (cfg->successors[edge] == to)
        {
            result = edge;
        }
    }
    return result;
}

static void ir_optimize_enqueue(IrOptimizeWork* work, uint32_t instruction)
{
    if (work->queued[instruction] == 0)
    {
        work->queued[instruction] = 1;
        work->queue[work->tail] = instruction;
        work->tail += 1;
        if (work->tail == work->function->instruction_count)
        {
            work->tail = 0;
        }
        work->queued_count += 1;
    }
}

static uint32_t ir_optimize_dequeue(IrOptimizeWork* work)
{
    const uint32_t instruction = work->queue[work->head];
    work->head += 1;
    if (work->head == work->function->instruction_count)
    {
        work->head = 0;
    }
    work->queued_count -= 1;
    work->queued[instruction] = 0;
    return instruction;
}

static void ir_optimize_enqueue_users(IrOptimizeWork* work, uint32_t value)
{
    for (uint32_t use = work->uses.offsets[value]; use < work->uses.offsets[(uint64_t)value + 1]; use += 1)
    {
        ir_optimize_enqueue(work, work->uses.uses[use].instruction);
    }
}

static uint8_t ir_optimize_meet(uint8_t left, IrConstant a, uint8_t right, IrConstant b, IrConstant* value)
{
    uint8_t result;
    if (left == IROPT_UNKNOWN)
    {
        result = right;
        *value = b;
    }
    else if (right == IROPT_UNKNOWN)
    {
        result = left;
        *value = a;
    }
    else if (left == IROPT_CONSTANT && right == IROPT_CONSTANT && ir_optimize_equal(a, b))
    {
        result = IROPT_CONSTANT;
        *value = a;
    }
    else
    {
        result = IROPT_OVERDEFINED;
    }
    return result;
}

static uint8_t ir_optimize_transfer(const IrOptimizeWork* work, uint32_t i, IrConstant* value)
{
    const IrFunction* f = work->function;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    uint8_t result = IROPT_OVERDEFINED;
    if (op == IR_CONST)
    {
        result = IROPT_CONSTANT;
        *value = f->constants[a];
    }
    else if (op == IR_COPY)
    {
        result = work->states[a];
        *value = work->values[a];
    }
    else if (op == IR_PHI)
    {
        result = IROPT_UNKNOWN;
        for (uint32_t incoming = 0; incoming < b; incoming += 1)
        {
            const uint32_t predecessor = f->extra[a + incoming * 2];
            const uint32_t source = f->extra[a + incoming * 2 + 1];
            const uint32_t edge = ir_optimize_edge(&work->cfg, predecessor, work->cfg.instruction_blocks[i]);
            if (work->edges[edge] != 0)
            {
                result = ir_optimize_meet(result, *value, work->states[source], work->values[source], value);
            }
        }
    }
    else if (op == IR_SELECT)
    {
        if (work->states[a] == IROPT_CONSTANT)
        {
            const uint32_t source = work->values[a].low != 0 ? b : f->c[i];
            result = work->states[source];
            *value = work->values[source];
        }
        else if (work->states[b] == IROPT_CONSTANT && work->states[f->c[i]] == IROPT_CONSTANT && ir_optimize_equal(work->values[b], work->values[f->c[i]]))
        {
            result = IROPT_CONSTANT;
            *value = work->values[b];
        }
        else if (work->states[a] == IROPT_UNKNOWN)
        {
            result = IROPT_UNKNOWN;
        }
    }
    else if (op == IR_NEG || op == IR_NOT || op == IR_TRUNC || op == IR_ZEXT || op == IR_SEXT || op == IR_INTTOPTR || op == IR_PTRTOINT || op == IR_BITCAST || op == IR_FNEG)
    {
        result = work->states[a];
        if (result == IROPT_CONSTANT)
        {
            unsigned __int128 bits = ir_optimize_bits(work->values[a]);
            const uint32_t source_width = ir_type_bits[f->type[a]];
            const uint32_t width = ir_type_bits[f->type[i]];
            if (op == IR_NEG)
            {
                bits = -bits;
            }
            else if (op == IR_NOT)
            {
                bits = ~bits;
            }
            else if (op == IR_SEXT && (bits & ((unsigned __int128)1 << (source_width - 1))) != 0)
            {
                bits |= ~(unsigned __int128)0 << source_width;
            }
            else if (op == IR_FNEG)
            {
                bits ^= (unsigned __int128)1 << (width - 1);
            }
            *value = ir_optimize_constant(bits, width);
        }
    }
    else
    {
        bool swap;
        const uint32_t operation = ir_optimize_fold_operation(f, i, &swap);
        if (operation != IROPT_FOLD_NONE)
        {
            const uint8_t left_state = work->states[a];
            const uint8_t right_state = work->states[b];
            if (left_state == IROPT_OVERDEFINED || right_state == IROPT_OVERDEFINED)
            {
                result = IROPT_OVERDEFINED;
            }
            else if (left_state == IROPT_UNKNOWN || right_state == IROPT_UNKNOWN)
            {
                result = IROPT_UNKNOWN;
            }
            else
            {
                const IrConstant left = work->values[swap ? b : a];
                const IrConstant right = work->values[swap ? a : b];
                const uint32_t width = ir_type_bits[f->type[a]];
                bool folded;
                if (width == 128)
                {
                    folded = ir_optimize_fold_wide(operation, left, right, value);
                }
                else
                {
                    folded = right.high == 0 && ir_optimize_fold_one(operation, width, left.low, right.low, &value->low);
                    value->high = 0;
                }
                result = folded ? IROPT_CONSTANT : IROPT_OVERDEFINED;
            }
        }
    }
    return result;
}

static bool ir_optimize_publish(IrOptimizeWork* work, uint32_t i, uint8_t state, IrConstant value, bool queue)
{
    IrConstant combined = { 0 };
    const uint8_t next = ir_optimize_meet(work->states[i], work->values[i], state, value, &combined);
    const bool changed = next != work->states[i];
    if (changed)
    {
        work->states[i] = next;
        work->values[i] = next == IROPT_CONSTANT ? combined : (IrConstant){ 0 };
        if (queue)
        {
            ir_optimize_enqueue_users(work, i);
        }
    }
    return changed;
}

static bool ir_optimize_mark_edge(IrOptimizeWork* work, uint32_t edge, bool queue)
{
    const bool changed = work->edges[edge] == 0;
    if (changed)
    {
        const uint32_t block = work->cfg.successors[edge];
        work->edges[edge] = 1;
        work->blocks[block] = 1;
        if (queue)
        {
            for (uint32_t i = work->function->block_offsets[block]; i < work->function->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                ir_optimize_enqueue(work, i);
            }
        }
    }
    return changed;
}

static bool ir_optimize_control(IrOptimizeWork* work, uint32_t i, bool queue)
{
    const IrFunction* f = work->function;
    const uint32_t block = work->cfg.instruction_blocks[i];
    const uint32_t op = f->op[i];
    uint32_t target = BUSTER_IR_NONE;
    bool all = false;
    if (op == IR_BR)
    {
        target = f->a[i];
    }
    else if (op == IR_BR_INDIRECT)
    {
        // It may go to any label: no address it holds is known to be one label's rather than another's.
        all = true;
    }
    else if (op == IR_CBR || op == IR_SWITCH)
    {
        const uint8_t state = work->states[f->a[i]];
        all = state == IROPT_OVERDEFINED;
        if (state == IROPT_CONSTANT)
        {
            if (op == IR_CBR)
            {
                target = work->values[f->a[i]].low != 0 ? f->b[i] : f->c[i];
            }
            else
            {
                target = f->extra[f->b[i]];
                for (uint32_t entry = 0; entry < f->c[i]; entry += 1)
                {
                    if (ir_optimize_equal(work->values[f->a[i]], f->constants[f->extra[f->b[i] + 1 + entry * 2]]))
                    {
                        target = f->extra[f->b[i] + 2 + entry * 2];
                    }
                }
            }
        }
    }
    bool changed = false;
    for (uint32_t edge = work->cfg.successor_offsets[block]; edge < work->cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
    {
        if (all || work->cfg.successors[edge] == target)
        {
            changed = ir_optimize_mark_edge(work, edge, queue) || changed;
        }
    }
    return changed;
}

static bool ir_optimize_visit(IrOptimizeWork* work, uint32_t i, bool queue)
{
    bool changed = false;
    if (work->blocks[work->cfg.instruction_blocks[i]] != 0)
    {
        IrConstant value = { 0 };
        const uint8_t state = ir_optimize_transfer(work, i, &value);
        changed = ir_optimize_publish(work, i, state, value, queue);
        if (ir_terminator_table[work->function->op[i]] != 0)
        {
            changed = ir_optimize_control(work, i, queue) || changed;
        }
        work->statistics.sccp_visits += 1;
    }
    return changed;
}

static bool ir_optimize_settle_unknown(IrOptimizeWork* work, bool queue)
{
    bool changed = false;
    for (uint32_t i = 0; i < work->function->instruction_count; i += 1)
    {
        if (work->blocks[work->cfg.instruction_blocks[i]] != 0 && work->states[i] == IROPT_UNKNOWN)
        {
            changed = ir_optimize_publish(work, i, IROPT_OVERDEFINED, (IrConstant){ 0 }, queue) || changed;
        }
    }
    return changed;
}

static void ir_optimize_sccp_worklist(IrOptimizeWork* work)
{
    for (uint32_t i = 0; i < work->function->instruction_count; i += 1)
    {
        if (work->blocks[work->cfg.instruction_blocks[i]] != 0)
        {
            ir_optimize_enqueue(work, i);
        }
    }
    bool pending = true;
    while (pending)
    {
        while (work->queued_count != 0)
        {
            ir_optimize_visit(work, ir_optimize_dequeue(work), true);
        }
        pending = ir_optimize_settle_unknown(work, true);
    }
}

static void ir_optimize_sccp_sweeps(IrOptimizeWork* work, uint32_t limit)
{
    const IrFunction* f = work->function;
    bool changed = true;
    uint32_t sweep = 0;
    while (changed && sweep < limit)
    {
        changed = false;
        for (uint32_t rank = 0; rank < work->dominators.reachable_count; rank += 1)
        {
            const uint32_t block = work->dominators.rpo[rank];
            if (work->blocks[block] != 0)
            {
                for (uint64_t first = f->block_offsets[block]; first < f->block_offsets[(uint64_t)block + 1]; first += BUSTER_IR_OPTIMIZE_BATCH)
                {
                    const uint32_t count = f->block_offsets[(uint64_t)block + 1] - first < BUSTER_IR_OPTIMIZE_BATCH ? f->block_offsets[(uint64_t)block + 1] - first : BUSTER_IR_OPTIMIZE_BATCH;
                    uint32_t operation[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
                    uint32_t width[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
                    uint64_t left[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
                    uint64_t right[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
                    uint64_t values[BUSTER_IR_OPTIMIZE_BATCH] = { 0 };
                    uint16_t active = 0;
                    for (uint32_t lane = 0; lane < count; lane += 1)
                    {
                        const uint32_t i = first + lane;
                        bool swap;
                        operation[lane] = ir_optimize_fold_operation(f, i, &swap);
                        if (operation[lane] != 0 && work->states[f->a[i]] == IROPT_CONSTANT && work->states[f->b[i]] == IROPT_CONSTANT && ir_type_bits[f->type[f->a[i]]] <= 64 && work->values[f->b[i]].high == 0)
                        {
                            active |= UINT32_C(1) << lane;
                            width[lane] = ir_type_bits[f->type[f->a[i]]];
                            left[lane] = work->values[swap ? f->b[i] : f->a[i]].low;
                            right[lane] = work->values[swap ? f->a[i] : f->b[i]].low;
                        }
                    }
                    const IrOptimizeFoldBatch batch = {
                        .count = count,
                        .operation = operation,
                        .width = width,
                        .left = left,
                        .right = right,
                        .result = values,
                    };
                    const uint16_t folded = ir_optimize_fold_vector(batch, active);
                    for (uint32_t lane = 0; lane < count; lane += 1)
                    {
                        const uint32_t i = first + lane;
                        if ((folded & (UINT32_C(1) << lane)) != 0)
                        {
                            const IrConstant value = { .low = values[lane] };
                            changed = ir_optimize_publish(work, i, IROPT_CONSTANT, value, false) || changed;
                            work->statistics.sccp_visits += 1;
                        }
                        else
                        {
                            changed = ir_optimize_visit(work, i, false) || changed;
                        }
                    }
                }
            }
        }
        sweep += 1;
        if (!changed)
        {
            changed = ir_optimize_settle_unknown(work, false);
        }
    }
    work->statistics.sccp_sweeps += sweep;
    if (changed)
    {
        work->statistics.worklist_fallback = true;
        ir_optimize_sccp_worklist(work);
    }
}

static uint32_t ir_optimize_single_successor(const IrOptimizeWork* work, uint32_t block)
{
    uint32_t target = BUSTER_IR_NONE;
    uint32_t count = 0;
    for (uint32_t edge = work->cfg.successor_offsets[block]; edge < work->cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
    {
        if (work->edges[edge] != 0)
        {
            target = work->cfg.successors[edge];
            count += 1;
        }
    }
    if (count != 1)
    {
        target = BUSTER_IR_NONE;
    }
    return target;
}

// 1 is an unconditional effect; 2 is a potential fault or floating-environment effect, discharged only by
// a successful constant transfer. Loads remain roots even when nonvolatile: there is no dereferenceability proof.
static const uint8_t ir_optimize_roots[64] = {
        [IR_LOAD] = 1,
        [IR_STORE] = 1,
        [IR_CALL] = 1,
        [IR_CALL_INDIRECT] = 1,
        [IR_ALLOCA] = 1,
        [IR_STACK_SAVE] = 1,
        [IR_STACK_RESTORE] = 1,
        [IR_BR] = 1,
        [IR_CBR] = 1,
        [IR_SWITCH] = 1,
        [IR_BR_INDIRECT] = 1,
        [IR_RET] = 1,
        [IR_UNREACHABLE] = 1,
        [IR_SDIV] = 2,
        [IR_UDIV] = 2,
        [IR_SREM] = 2,
        [IR_UREM] = 2,
        [IR_SHL] = 2,
        [IR_LSHR] = 2,
        [IR_ASHR] = 2,
        [IR_FADD] = 2,
        [IR_FSUB] = 2,
        [IR_FMUL] = 2,
        [IR_FDIV] = 2,
        [IR_FCMP] = 2,
        [IR_FTRUNC] = 2,
        [IR_FEXT] = 2,
        [IR_SITOFP] = 2,
        [IR_UITOFP] = 2,
        [IR_FPTOSI] = 2,
        [IR_FPTOUI] = 2,
};

static bool ir_optimize_root(const IrOptimizeWork* work, uint32_t i)
{
    const uint32_t kind = ir_optimize_roots[work->function->op[i]];
    const bool result = kind == 1 || (kind == 2 && work->states[i] != IROPT_CONSTANT);
    return result;
}

static bool ir_optimize_mark_operands(IrOptimizeWork* work, uint32_t i, bool queue)
{
    const IrFunction* f = work->function;
    bool changed = false;
    const bool collapsed_branch = (f->op[i] == IR_CBR || f->op[i] == IR_SWITCH) && ir_optimize_single_successor(work, work->cfg.instruction_blocks[i]) != BUSTER_IR_NONE;
    if (work->states[i] != IROPT_CONSTANT && !collapsed_branch)
    {
        const uint64_t count = ir_operand_count(f, i);
        for (uint32_t number = 0; number < count; number += 1)
        {
            const IrOperand operand = ir_operand(f, i, number, work->cfg.instruction_blocks[i]);
            const bool executable = f->op[i] != IR_PHI || work->edges[ir_optimize_edge(&work->cfg, operand.block, work->cfg.instruction_blocks[i])] != 0;
            if (executable && work->live[operand.value] == 0)
            {
                work->live[operand.value] = 1;
                changed = true;
                if (queue)
                {
                    ir_optimize_enqueue(work, operand.value);
                }
            }
        }
    }
    work->statistics.dce_visits += 1;
    return changed;
}

static void ir_optimize_dce_worklist(IrOptimizeWork* work)
{
    for (uint32_t i = 0; i < work->function->instruction_count; i += 1)
    {
        if (work->live[i] != 0)
        {
            ir_optimize_enqueue(work, i);
        }
    }
    while (work->queued_count != 0)
    {
        ir_optimize_mark_operands(work, ir_optimize_dequeue(work), true);
    }
}

static void ir_optimize_dce(IrOptimizeWork* work, IrOptimizeOptions options)
{
    const IrFunction* f = work->function;
    for (uint32_t block = 0; block < f->block_count; block += 1)
    {
        if (work->blocks[block] != 0)
        {
            const uint32_t first = f->block_offsets[block];
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            if ((options.passes & BUSTER_IR_OPTIMIZE_DCE) == 0)
            {
                memset(work->live + first, 1, end - first);
            }
            else if (BUSTER_SCALAR_PATH(options.scalar))
            {
                for (uint32_t i = first; i < end; i += 1)
                {
                    work->live[i] = ir_optimize_root(work, i);
                }
            }
            else
            {
                for (uint64_t at = first; at < end; at += BUSTER_IR_OPTIMIZE_BATCH)
                {
                    const uint32_t count = end - at < BUSTER_IR_OPTIMIZE_BATCH ? end - at : BUSTER_IR_OPTIMIZE_BATCH;
                    const __mmask16 valid = (UINT32_C(1) << count) - 1;
                    const __m128i op = _mm_maskz_loadu_epi8(valid, f->op + at);
                    const __m128i kinds = _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_zextsi128_si512(op), _mm512_loadu_si512(ir_optimize_roots)));
                    const __m128i states = _mm_maskz_loadu_epi8(valid, work->states + at);
                    const __mmask16 roots = valid & (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(1)) | (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(2)) & _mm_cmpneq_epi8_mask(states, _mm_set1_epi8(IROPT_CONSTANT))));
                    _mm_mask_storeu_epi8(work->live + at, valid, _mm_maskz_set1_epi8(roots, 1));
                }
            }
        }
    }
    if ((options.passes & BUSTER_IR_OPTIMIZE_DCE) != 0)
    {
        if (options.scalar)
        {
            ir_optimize_dce_worklist(work);
        }
        else
        {
            bool changed = true;
            uint32_t sweep = 0;
            while (changed && sweep < options.sweep_limit)
            {
                changed = false;
                for (uint32_t end = f->instruction_count; end != 0;)
                {
                    const uint32_t count = end < BUSTER_IR_OPTIMIZE_BATCH ? end : BUSTER_IR_OPTIMIZE_BATCH;
                    const uint32_t first = end - count;
                    const __m128i live = _mm_maskz_loadu_epi8((UINT32_C(1) << count) - 1, work->live + first);
                    if (_mm_cmpneq_epi8_mask(live, _mm_setzero_si128()) != 0)
                    {
                        // The reverse in-batch walk observes operands marked by a later lane immediately. Snapshotting
                        // one mask for all lanes would add up to 16 rounds to a straight dependency chain.
                        for (uint32_t i = end; i > first; i -= 1)
                        {
                            if (work->live[i - 1] != 0)
                            {
                                changed = ir_optimize_mark_operands(work, i - 1, false) || changed;
                            }
                        }
                    }
                    end = first;
                }
                sweep += 1;
            }
            work->statistics.dce_sweeps = sweep;
            if (changed)
            {
                work->statistics.worklist_fallback = true;
                ir_optimize_dce_worklist(work);
            }
        }
    }
}

static IrCheck ir_optimize_prepare(IrOptimizeWork* work, const IrFunction* function, Arena* output, Arena* scratch, bool scalar)
{
    (void)scalar;

    work->function = function;
    IrCheck check = BUSTER_SCALAR_PATH(scalar) ? ir_verify_scalar(function, scratch, 0) : ir_verify(function, scratch, 0);
    // Entry phis are forbidden by the shared IR contract: initial invocation has no predecessor value.
    // Pruning their dead predecessors cannot invent an undef value or emit a zero-input phi.
    if (check.error == IR_OK && function->block_count != 0 && function->op[0] == IR_PHI)
    {
        check = ir_check(IR_ERROR_PHI, 0, 0);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_build_cfg(function, scratch, output, &work->cfg);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_build_uses(function, scratch, output, &work->uses);
    }
    if (check.error == IR_OK)
    {
        const DominatorCfg cfg = {
            .block_count = work->cfg.block_count,
            .edge_count = work->cfg.edge_count,
            .entry = function->block_count == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = work->cfg.successor_offsets,
            .successors = work->cfg.successors,
            .predecessor_offsets = work->cfg.predecessor_offsets,
            .predecessors = work->cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        options.path = BUSTER_SCALAR_PATH(scalar) ? DOMINATOR_SCALAR : DOMINATOR_AUTO;
        const DominatorStatus status = dominators_analyze(scratch, output, &cfg, options, &work->dominators);
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_DOMINANCE;
    }
    // Dead regions are valid IR. Check SSA for executable source regions without rejecting dead cross-block uses;
    // their instructions disappear before the final full dominance verifier runs.
    for (uint32_t i = 0; check.error == IR_OK && i < function->instruction_count; i += 1)
    {
        const uint64_t count = ir_operand_count(function, i);
        for (uint32_t number = 0; check.error == IR_OK && number < count; number += 1)
        {
            const IrOperand operand = ir_operand(function, i, number, work->cfg.instruction_blocks[i]);
            if (work->dominators.rpo_index[operand.block] != BUSTER_DOMINATOR_NONE && !dominators_dominates(&work->dominators, work->cfg.instruction_blocks[operand.value], operand.block))
            {
                check = ir_check(IR_ERROR_DOMINANCE, i, operand.position);
            }
        }
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        work->states = ir_allocate(scratch, function->instruction_count, sizeof(uint8_t), &ok);
        work->values = ir_allocate(scratch, function->instruction_count, sizeof(IrConstant), &ok);
        work->blocks = ir_allocate(scratch, function->block_count, sizeof(uint8_t), &ok);
        work->edges = ir_allocate(scratch, work->cfg.edge_count, sizeof(uint8_t), &ok);
        work->live = ir_allocate(scratch, function->instruction_count, sizeof(uint8_t), &ok);
        work->queued = ir_allocate(scratch, function->instruction_count, sizeof(uint8_t), &ok);
        work->queue = ir_allocate(scratch, function->instruction_count, sizeof(uint32_t), &ok);
        if (!ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
    }
    return check;
}

static uint32_t ir_optimize_phi_inputs(const IrOptimizeWork* work, uint32_t instruction)
{
    const IrFunction* f = work->function;
    uint32_t count = 0;
    for (uint32_t incoming = 0; incoming < f->b[instruction]; incoming += 1)
    {
        const uint32_t predecessor = f->extra[f->a[instruction] + incoming * 2];
        count += work->edges[ir_optimize_edge(&work->cfg, predecessor, work->cfg.instruction_blocks[instruction])] != 0;
    }
    return count;
}

static IrError ir_optimize_rewrite(IrOptimizeWork* work, Arena* output, Arena* scratch, IrOptimizeOptions options, IrFunction* result)
{
    const IrFunction* f = work->function;
    bool ok = true;
    uint32_t* direct = ir_allocate(scratch, f->block_count, sizeof(uint32_t), &ok);
    uint32_t* threads = ir_allocate(scratch, f->block_count, sizeof(uint32_t), &ok);
    uint32_t* blocks = ir_allocate(scratch, f->block_count, sizeof(uint32_t), &ok);
    uint32_t* values = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    for (uint32_t block = 0; ok && block < f->block_count; block += 1)
    {
        direct[block] = block;
        blocks[block] = BUSTER_IR_NONE;
        uint32_t live = 0;
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            live += work->live[i] != 0;
        }
        const uint32_t target = ir_optimize_single_successor(work, block);
        if ((options.passes & BUSTER_IR_OPTIMIZE_CFG) != 0 && block != 0 && work->blocks[block] != 0 && live == 1 && target != BUSTER_IR_NONE && f->op[f->block_offsets[target]] != IR_PHI)
        {
            direct[block] = target;
        }
    }
    if (ok && !ir_optimize_thread_targets(scratch, direct, f->block_count, threads, BUSTER_SCALAR_PATH(options.scalar)))
    {
        error = IR_ERROR_MEMORY;
    }
    IrSizes sizes = {
        .constant_count = f->constant_count,
        .slot_count = f->slot_count,
        .address_count = f->address_count,
        .call_type_count = f->call_type_count,
        .parameter_count = f->parameter_count,
        .label_count = f->label_count,
        .abi_call_count = f->abi_call_count,
        .passed_count = f->passed_count,
    };
    for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
    {
        values[i] = BUSTER_IR_NONE;
    }
    for (uint32_t block = 0; error == IR_OK && block < f->block_count; block += 1)
    {
        if (work->blocks[block] != 0 && threads[block] == block)
        {
            blocks[block] = sizes.block_count++;
            // A folded phi becomes a constant after every surviving phi. Ordinary instruction order is unchanged.
            for (uint32_t phase = 0; phase < 2; phase += 1)
            {
                for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
                {
                    const bool phi = f->op[i] == IR_PHI && work->states[i] != IROPT_CONSTANT;
                    if (work->live[i] != 0 && phi == (phase == 0))
                    {
                        values[i] = sizes.instruction_count++;
                        if (work->states[i] == IROPT_CONSTANT && f->op[i] != IR_CONST)
                        {
                            sizes.constant_count += 1;
                        }
                        else if (f->op[i] == IR_PHI)
                        {
                            sizes.extra_count += (uint64_t)ir_optimize_phi_inputs(work, i) * 2;
                        }
                        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
                        {
                            sizes.call_count += 1;
                            sizes.extra_count += f->calls[f->b[i]].argument_count;
                        }
                        else if (f->op[i] == IR_SWITCH && ir_optimize_single_successor(work, block) == BUSTER_IR_NONE)
                        {
                            sizes.extra_count += 1 + (uint64_t)f->c[i] * 2;
                        }
                    }
                }
            }
        }
    }
    IrFunction candidate = { 0 };
    if (error == IR_OK)
    {
        error = ir_create(output, &sizes, &candidate);
    }
    if (error == IR_OK)
    {
        candidate.result_type = f->result_type;
        candidate.symbol_count = f->symbol_count;
        if (f->constant_count != 0)
        {
            memcpy(candidate.constants, f->constants, (uint64_t)f->constant_count * sizeof(IrConstant));
        }
        if (f->slot_count != 0)
        {
            memcpy(candidate.slots, f->slots, (uint64_t)f->slot_count * sizeof(IrSlot));
        }
        if (f->address_count != 0)
        {
            memcpy(candidate.addresses, f->addresses, (uint64_t)f->address_count * sizeof(IrAddress));
        }
        if (f->call_type_count != 0)
        {
            memcpy(candidate.call_types, f->call_types, f->call_type_count);
        }
        if (f->parameter_count != 0)
        {
            memcpy(candidate.parameter_types, f->parameter_types, f->parameter_count);
        }
        // The ABI records and the incoming frame as they were (each call keeps its index), and each label at its block
        // as threaded: a label whose block is dead has none, which only a function no br_indirect reaches leaves.
        if (f->abi_call_count != 0)
        {
            memcpy(candidate.abi_calls, f->abi_calls, (uint64_t)f->abi_call_count * sizeof(IrAbiCall));
        }
        if (f->passed_count != 0)
        {
            memcpy(candidate.passed, f->passed, (uint64_t)f->passed_count * sizeof(IrPassed));
        }
        for (uint32_t label = 0; label < f->label_count; label += 1)
        {
            const uint32_t block = f->labels[label];
            candidate.labels[label] = block == BUSTER_IR_NONE || work->blocks[block] == 0 ? BUSTER_IR_NONE : blocks[threads[block]];
        }
        candidate.parameter_passed = f->parameter_passed;
        candidate.result_size = f->result_size;
        candidate.result_abi = f->result_abi;
        candidate.variadic = f->variadic;
        candidate.incoming = f->incoming;
        uint32_t extra = 0;
        uint32_t constants = f->constant_count;
        uint32_t calls = 0;
        uint32_t instructions = 0;
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            if (blocks[block] != BUSTER_IR_NONE)
            {
                candidate.block_offsets[blocks[block]] = instructions;
                for (uint32_t phase = 0; phase < 2; phase += 1)
                {
                    for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
                    {
                        const bool phi = f->op[i] == IR_PHI && work->states[i] != IROPT_CONSTANT;
                        if (values[i] != BUSTER_IR_NONE && phi == (phase == 0))
                        {
                            const uint32_t out = values[i];
                            const uint32_t op = f->op[i];
                            candidate.op[out] = op;
                            candidate.type[out] = f->type[i];
                            candidate.flags[out] = f->flags[i];
                            candidate.alignment_log2[out] = f->alignment_log2[i];
                            candidate.a[out] = f->a[i];
                            candidate.b[out] = f->b[i];
                            candidate.c[out] = f->c[i];
                            const uint32_t target = ir_optimize_single_successor(work, block);
                            if (work->states[i] == IROPT_CONSTANT && op != IR_CONST)
                            {
                                candidate.op[out] = IR_CONST;
                                candidate.a[out] = constants;
                                candidate.b[out] = BUSTER_IR_NONE;
                                candidate.c[out] = BUSTER_IR_NONE;
                                candidate.flags[out] = 0;
                                candidate.alignment_log2[out] = 0;
                                candidate.constants[constants++] = work->values[i];
                                work->statistics.folded += 1;
                            }
                            else if (op == IR_BR || ((op == IR_CBR || op == IR_SWITCH) && target != BUSTER_IR_NONE))
                            {
                                candidate.op[out] = IR_BR;
                                candidate.a[out] = blocks[threads[target]];
                                candidate.b[out] = BUSTER_IR_NONE;
                                candidate.c[out] = BUSTER_IR_NONE;
                            }
                            else
                            {
                                const uint32_t mask = ir_value_mask(f, i);
                                if ((mask & 1) != 0)
                                {
                                    candidate.a[out] = values[f->a[i]];
                                }
                                if ((mask & 2) != 0)
                                {
                                    candidate.b[out] = values[f->b[i]];
                                }
                                if ((mask & 4) != 0)
                                {
                                    candidate.c[out] = values[f->c[i]];
                                }
                                if (op == IR_PHI)
                                {
                                    candidate.a[out] = extra;
                                    candidate.b[out] = 0;
                                    for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
                                    {
                                        const uint32_t predecessor = f->extra[f->a[i] + incoming * 2];
                                        if (work->edges[ir_optimize_edge(&work->cfg, predecessor, block)] != 0)
                                        {
                                            candidate.extra[extra++] = blocks[predecessor];
                                            candidate.extra[extra++] = values[f->extra[f->a[i] + incoming * 2 + 1]];
                                            candidate.b[out] += 1;
                                        }
                                    }
                                }
                                else if (op == IR_CALL || op == IR_CALL_INDIRECT)
                                {
                                    candidate.b[out] = calls;
                                    candidate.calls[calls] = f->calls[f->b[i]];
                                    candidate.calls[calls].argument_first = extra;
                                    for (uint32_t argument = 0; argument < f->calls[f->b[i]].argument_count; argument += 1)
                                    {
                                        candidate.extra[extra++] = values[f->extra[f->calls[f->b[i]].argument_first + argument]];
                                    }
                                    calls += 1;
                                }
                                else if (op == IR_CBR)
                                {
                                    candidate.b[out] = blocks[threads[f->b[i]]];
                                    candidate.c[out] = blocks[threads[f->c[i]]];
                                }
                                else if (op == IR_SWITCH)
                                {
                                    candidate.b[out] = extra;
                                    candidate.extra[extra++] = blocks[threads[f->extra[f->b[i]]]];
                                    for (uint32_t entry = 0; entry < f->c[i]; entry += 1)
                                    {
                                        candidate.extra[extra++] = f->extra[f->b[i] + 1 + entry * 2];
                                        candidate.extra[extra++] = blocks[threads[f->extra[f->b[i] + 2 + entry * 2]]];
                                    }
                                }
                            }
                            instructions += 1;
                        }
                    }
                }
            }
        }
        candidate.block_offsets[candidate.block_count] = candidate.instruction_count;
        work->statistics.removed_instructions = f->instruction_count - candidate.instruction_count;
        work->statistics.removed_blocks = f->block_count - candidate.block_count;
        *result = candidate;
    }
    return error;
}

static IrCheck ir_optimize_verify_result(const IrFunction* function, Arena* output, Arena* scratch, bool scalar)
{
    (void)scalar;

    const uint64_t mark = scratch->length;
    IrOptimizeWork verification = { 0 };
    IrCheck check = ir_optimize_prepare(&verification, function, output, scratch, BUSTER_SCALAR_PATH(scalar));
    if (check.error == IR_OK)
    {
        const IrDominance dominance = {
            .enter = verification.dominators.preorder,
            .leave = verification.dominators.subtree_end,
            .block_count = function->block_count,
        };
        check = BUSTER_SCALAR_PATH(scalar) ? ir_verify_scalar(function, scratch, &dominance) : ir_verify(function, scratch, &dominance);
    }
    arena_pop_to(scratch, mark);
    return check;
}

static IrCheck ir_optimize(const IrFunction* function, Arena* output, Arena* scratch, IrOptimizeOptions options, IrOptimizeResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrOptimizeWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, options.passes);
    if (output != scratch && (options.passes & ~(BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG)) == 0)
    {
        check = ir_optimize_prepare(&work, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    if (check.error == IR_OK)
    {
        if (function->block_count != 0)
        {
            work.blocks[0] = 1;
        }
        if ((options.passes & BUSTER_IR_OPTIMIZE_SCCP) != 0)
        {
            if (options.scalar)
            {
                ir_optimize_sccp_worklist(&work);
            }
            else
            {
                ir_optimize_sccp_sweeps(&work, options.sweep_limit);
            }
        }
        else
        {
            for (uint32_t block = 0; block < function->block_count; block += 1)
            {
                work.blocks[block] = work.dominators.rpo_index[block] != BUSTER_DOMINATOR_NONE;
                for (uint32_t edge = work.cfg.successor_offsets[block]; edge < work.cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
                {
                    work.edges[edge] = work.blocks[block];
                }
            }
            for (uint32_t i = 0; i < function->instruction_count; i += 1)
            {
                work.states[i] = function->op[i] == IR_CONST ? IROPT_CONSTANT : IROPT_OVERDEFINED;
                if (function->op[i] == IR_CONST)
                {
                    work.values[i] = function->constants[function->a[i]];
                }
            }
        }
        ir_optimize_dce(&work, options);
        check.error = ir_optimize_rewrite(&work, output, scratch, options, &work.statistics.function);
    }
    if (check.error == IR_OK)
    {
        check = ir_optimize_verify_result(&work.statistics.function, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    if (check.error == IR_OK)
    {
        work.statistics.scratch_bytes = scratch->length - scratch_mark;
        *result = work.statistics;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}
