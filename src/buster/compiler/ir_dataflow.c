// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared IR -> dense inter-block liveness (#143). Include after lane.c, ir.c and dataflow.c. This is the raw IR's
// analysis, not the selected-use analysis of tier1_liveness.c: instruction selection can remove or introduce uses.
// Ordinary block-local values have no bit. Every phi input has one, including an input defined in its predecessor:
// it is consumed on that edge and must appear in the predecessor's live-out, not in the phi block's live-in.
//
// Preparing checks the IR's structure, operand types, local order and phi predecessors, but does not prove cross-
// block dominance. Input/output/scratch storage is disjoint. Both arenas and *result are transactional on failure;
// scratch is returned on success too. The result owns all its columns and does not borrow the function or CFG.
//
// Phi edge uses need no extra dense matrix and no change to dataflow.c's measured fixpoints. Upward-exposed phi
// inputs join their predecessor's use row before solving. After solving, ALL phi inputs join that predecessor's out
// row. This is exact: a phi input defined there is killed by def; every other one was already in use. Solvers read
// successor in, never out. A repeated solve first clears in as usual and overwrites out before restoring edge uses.
// See docs/ir-dataflow.md for the equations, local-value and padding contracts, tests and measurement boundaries.

#ifndef BUSTER_IR_DATAFLOW_C
#define BUSTER_IR_DATAFLOW_C

// AVX-512 gathers use signed 32-bit element indices; both paths expose the same explicit bound.
#define BUSTER_IR_DATAFLOW_MAX_INSTRUCTIONS INT32_MAX

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

typedef struct IrDataflow IrDataflow;
struct IrDataflow
{
    Dataflow flow;
    uint32_t instruction_count;
    // Original instruction -> dense bit, or NONE; dense bit -> original instruction, in increasing ID order.
    uint32_t* ids;
    uint32_t* values;
    // Outgoing phi inputs, grouped by predecessor. Values are dense IDs; targets are successor block IDs. Duplicate
    // incoming values are retained, including equal values used by different phis or different outgoing edges.
    uint32_t* phi_offsets;
    uint32_t* phi_values;
    uint32_t* phi_targets;
    uint32_t phi_use_count;
    uint64_t arena_bytes;
};

// Up to sixteen IDs, NONE inactive. Group updates to the SAME 64-bit word before storing, with no hardware scatter,
// no aliasing through uint32_t*, and no lost updates when IDs repeat. The row needs only the words containing its IDs.
static void ir_dataflow_set(uint64_t* row, const uint32_t* ids, uint32_t count, bool vector)
{
    (void)vector;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __mmask16 tail = (__mmask16)((1u << count) - 1);
        const __m512i values = _mm512_maskz_loadu_epi32(tail, ids);
        const __mmask16 active = tail & _mm512_cmpneq_epi32_mask(values, _mm512_set1_epi32(-1));
        const __m512i words = _mm512_srli_epi32(values, 6);
        const __m512i previous = _mm512_and_si512(_mm512_conflict_epi32(words), _mm512_set1_epi32(active));
        uint32_t leaders = active & _mm512_cmpeq_epi32_mask(previous, _mm512_setzero_si512());
        const __m512i shifts = _mm512_and_si512(values, _mm512_set1_epi32(63));
        const __m512i low = _mm512_sllv_epi64(_mm512_set1_epi64(1), _mm512_cvtepu32_epi64(_mm512_castsi512_si256(shifts)));
        const __m512i high = _mm512_sllv_epi64(_mm512_set1_epi64(1), _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(shifts, 1)));
        uint32_t word_ids[16];
        _mm512_storeu_si512(word_ids, words);
        while (leaders != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(leaders);
            const uint32_t word = word_ids[lane];
            const __mmask16 group = active & _mm512_cmpeq_epi32_mask(words, _mm512_set1_epi32((int)word));
            row[word] |= (uint64_t)_mm512_mask_reduce_or_epi64((__mmask8)group, low) |
                         (uint64_t)_mm512_mask_reduce_or_epi64((__mmask8)(group >> 8), high);
            leaders &= leaders - 1;
        }
    }
    else
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t id = ids[i];
            if (id != BUSTER_IR_NONE)
            {
                row[id >> 6] |= UINT64_C(1) << (id & 63);
            }
        }
    }
}

// The selected ordinary operands of one block, either marking the original IDs that cross blocks (flow == NULL),
// or adding their dense IDs to use. Inactive columns can contain metadata, NONE, or values outside the function:
// they NEVER participate in a gather. Logical tails need no readable padding.
static void ir_dataflow_operands(const uint32_t* values, uint32_t count, uint32_t selected, uint32_t block,
                                 const IrCfg* cfg, uint64_t* cross, IrDataflow* flow, bool vector)
{
    (void)vector;
    uint32_t picked[16];
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __mmask16 active = (__mmask16)selected;
        const __m512i operands = _mm512_maskz_loadu_epi32(active, values);
        const __m512i definitions = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, operands, cfg->instruction_blocks, 4);
        const __mmask16 nonlocal = active & _mm512_cmpneq_epi32_mask(definitions, _mm512_set1_epi32((int)block));
        const __m512i ids = flow == NULL ? operands :
            _mm512_mask_i32gather_epi32(_mm512_set1_epi32(-1), nonlocal, operands, flow->ids, 4);
        _mm512_storeu_si512(picked, _mm512_mask_mov_epi32(_mm512_set1_epi32(-1), nonlocal, ids));
    }
    else
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            picked[i] = BUSTER_IR_NONE;
            if ((selected & (1u << i)) != 0 && cfg->instruction_blocks[values[i]] != block)
            {
                picked[i] = flow == NULL ? values[i] : flow->ids[values[i]];
            }
        }
    }
    uint64_t* row = flow == NULL ? cross : flow->flow.use + (uint64_t)block * flow->flow.row_words;
    ir_dataflow_set(row, picked, count, BUSTER_VECTOR_PATH(vector));
}

// Fixed operands from sixteen instruction columns at a time. The IR's required/optional value masks exclude
// predicates, symbols, slots, switch cases, labels and other metadata. Calls' variable argument slices are batches
// too. The scalar twin enumerates individual operands instead, including the indirect callee and aggregate result.
static void ir_dataflow_ordinary(const IrFunction* function, const IrCfg* cfg, uint64_t* cross, IrDataflow* flow, bool vector)
{
    (void)vector;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i required_table = _mm512_loadu_si512(ir_operand_table);
        const __m512i optional_table = _mm512_loadu_si512(ir_optional_table);
        const uint32_t* const columns[3] = { function->a, function->b, function->c };
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            const uint32_t end = function->block_offsets[block + 1];
            for (uint32_t first = function->block_offsets[block]; first < end;)
            {
                const uint32_t count = end - first < 16 ? end - first : 16;
                const __mmask16 tail = (__mmask16)((1u << count) - 1);
                const __m128i bytes = _mm_maskz_loadu_epi8(tail, function->op + first);
                const __m512i keys = _mm512_zextsi128_si512(bytes);
                const __m512i ops = _mm512_cvtepu8_epi32(bytes);
                const __m512i required = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, required_table)));
                const __m512i optional = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, optional_table)));
                for (uint32_t field = 0; field < 3; field += 1)
                {
                    const __m512i bit = _mm512_set1_epi32(1u << field);
                    const __m512i values = _mm512_maskz_loadu_epi32(tail, columns[field] + first);
                    const __mmask16 present = _mm512_cmpneq_epi32_mask(values, _mm512_set1_epi32(-1));
                    const __mmask16 selected = tail & (_mm512_test_epi32_mask(required, bit) | (_mm512_test_epi32_mask(optional, bit) & present));
                    ir_dataflow_operands(columns[field] + first, count, selected, block, cfg, cross, flow, true);
                }
                uint32_t calls = tail & (_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(IR_CALL)) |
                                         _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(IR_CALL_INDIRECT)));
                while (calls != 0)
                {
                    const uint32_t node = first + (uint32_t)__builtin_ctz(calls);
                    const IrCall call = function->calls[function->b[node]];
                    for (uint32_t argument = 0; argument < call.argument_count;)
                    {
                        const uint32_t length = call.argument_count - argument < 16 ? call.argument_count - argument : 16;
                        ir_dataflow_operands(function->extra + call.argument_first + argument, length, (1u << length) - 1,
                                             block, cfg, cross, flow, true);
                        argument += length;
                    }
                    calls &= calls - 1;
                }
                first += count;
            }
        }
    }
    else
    {
        for (uint32_t node = 0; node < function->instruction_count; node += 1)
        {
            if (function->op[node] != IR_PHI)
            {
                const uint32_t block = cfg->instruction_blocks[node];
                const uint64_t count = ir_operand_count(function, node);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const IrOperand operand = ir_operand(function, node, i, block);
                    if (cfg->instruction_blocks[operand.value] != block)
                    {
                        const uint32_t id = flow == NULL ? operand.value : flow->ids[operand.value];
                        uint64_t* row = flow == NULL ? cross : flow->flow.use + (uint64_t)block * flow->flow.row_words;
                        ir_dataflow_set(row, &id, 1, false);
                    }
                }
            }
        }
    }
}

// Separate the IR's interleaved predecessor/value pairs. A final slice of at most eight pairs has no second load,
// even a zero-masked one: do not form an out-of-object pointer to the next sixteen words.
static void ir_dataflow_phi_batch(const uint32_t* pairs, uint32_t count, uint32_t* blocks, uint32_t* values, bool vector)
{
    (void)vector;
    if (BUSTER_VECTOR_PATH(vector))
    {
        const uint32_t low_count = count < 8 ? count * 2 : 16;
        const __m512i low = _mm512_maskz_loadu_epi32((__mmask16)((1u << low_count) - 1), pairs);
        __m512i high = _mm512_setzero_si512();
        if (count > 8)
        {
            high = _mm512_maskz_loadu_epi32((__mmask16)((1u << ((count - 8) * 2)) - 1), pairs + 16);
        }
        const __m512i even = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
        const __m512i odd = _mm512_add_epi32(even, _mm512_set1_epi32(1));
        _mm512_storeu_si512(blocks, _mm512_permutex2var_epi32(low, even, high));
        _mm512_storeu_si512(values, _mm512_permutex2var_epi32(low, odd, high));
    }
    else
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            blocks[i] = pairs[i * 2];
            values[i] = pairs[i * 2 + 1];
        }
    }
}

static uint32_t ir_dataflow_count(const uint64_t* cross, uint32_t words, bool vector)
{
    (void)vector;
    uint32_t count = 0;
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; first < words;)
        {
            const uint32_t length = words - first < 8 ? words - first : 8;
            const __m512i bits = _mm512_maskz_loadu_epi64((__mmask8)((1u << length) - 1), cross + first);
            count += (uint32_t)_mm512_reduce_add_epi64(_mm512_popcnt_epi64(bits));
            first += length;
        }
    }
    else
    {
        for (uint32_t word = 0; word < words; word += 1)
        {
            count += (uint32_t)__builtin_popcountll(cross[word]);
        }
    }
    return count;
}

static void ir_dataflow_dense(const uint64_t* cross, IrDataflow* result, bool vector)
{
    (void)vector;
    const uint32_t count = result->instruction_count;
    uint32_t dense = 0;
    if (count != 0)
    {
        memset(result->ids, 0xff, (uint64_t)count * sizeof(uint32_t));
    }
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        const __m512i prefixes = _mm512_setr_epi32(0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767);
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t length = count - first < 16 ? count - first : 16;
            const __mmask16 selected = (__mmask16)((cross[first >> 6] >> (first & 63)) & ((1u << length) - 1));
            if (selected != 0)
            {
                const __m512i rank = _mm512_popcnt_epi32(_mm512_and_si512(prefixes, _mm512_set1_epi32(selected)));
                _mm512_mask_storeu_epi32(result->ids + first, selected, _mm512_add_epi32(rank, _mm512_set1_epi32((int)dense)));
                _mm512_mask_compressstoreu_epi32(result->values + dense, selected, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first)));
                dense += (uint32_t)__builtin_popcount((uint32_t)selected);
            }
            first += length;
        }
    }
    else
    {
        for (uint32_t value = 0; value < count; value += 1)
        {
            if ((cross[value >> 6] & (UINT64_C(1) << (value & 63))) != 0)
            {
                result->ids[value] = dense;
                result->values[dense] = value;
                dense += 1;
            }
        }
    }
}

static IrCheck ir_dataflow_prepare_path(const IrFunction* function, Arena* output, Arena* scratch, IrDataflow* result, bool vector)
{
    (void)vector;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrCheck check = output == scratch ? ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0) :
        function->instruction_count > BUSTER_IR_DATAFLOW_MAX_INSTRUCTIONS ? ir_check(IR_ERROR_LIMIT, BUSTER_IR_NONE, 0) : ir_check_shape(function);
    IrCfg cfg = { 0 };
    if (check.error == IR_OK)
    {
        check = ir_check_rows(function, BUSTER_VECTOR_PATH(vector));
    }
    if (check.error == IR_OK)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &cfg);
    }
    if (check.error == IR_OK)
    {
        check = ir_check_graph(function, &cfg, scratch, NULL, BUSTER_VECTOR_PATH(vector));
    }
    IrDataflow candidate = { .instruction_count = function->instruction_count };
    bool allocated = true;
    const uint32_t cross_words = (uint32_t)(((uint64_t)function->instruction_count + 63) / 64);
    uint64_t* cross = NULL;
    uint32_t* cursor = NULL;
    if (check.error == IR_OK)
    {
        cross = ir_allocate(scratch, cross_words, sizeof(uint64_t), &allocated);
        cursor = ir_allocate(scratch, function->block_count, sizeof(uint32_t), &allocated);
        candidate.ids = ir_allocate(output, function->instruction_count, sizeof(uint32_t), &allocated);
        candidate.phi_offsets = ir_allocate(output, (uint64_t)function->block_count + 1, sizeof(uint32_t), &allocated);
        check.error = allocated ? IR_OK : IR_ERROR_MEMORY;
    }
    uint64_t phi_count = 0;
    if (check.error == IR_OK)
    {
        ir_dataflow_ordinary(function, &cfg, cross, NULL, BUSTER_VECTOR_PATH(vector));
    }
    for (uint32_t block = 0; check.error == IR_OK && block < function->block_count; block += 1)
    {
        for (uint32_t node = function->block_offsets[block]; check.error == IR_OK && node < function->block_offsets[block + 1] && function->op[node] == IR_PHI; node += 1)
        {
            phi_count += function->b[node];
            if (phi_count > UINT32_MAX)
            {
                check = ir_check(IR_ERROR_LIMIT, node, 0);
            }
            for (uint32_t first = 0; check.error == IR_OK && first < function->b[node];)
            {
                const uint32_t count = function->b[node] - first < 16 ? function->b[node] - first : 16;
                uint32_t blocks[16];
                uint32_t values[16];
                ir_dataflow_phi_batch(function->extra + function->a[node] + (uint64_t)first * 2, count, blocks, values, BUSTER_VECTOR_PATH(vector));
                ir_dataflow_set(cross, values, count, BUSTER_VECTOR_PATH(vector));
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    candidate.phi_offsets[blocks[lane] + 1] += 1;
                }
                first += count;
            }
        }
    }
    if (check.error == IR_OK)
    {
        const uint32_t values = ir_dataflow_count(cross, cross_words, BUSTER_VECTOR_PATH(vector));
        candidate.phi_use_count = (uint32_t)phi_count;
        candidate.values = ir_allocate(output, values, sizeof(uint32_t), &allocated);
        candidate.phi_values = ir_allocate(output, phi_count, sizeof(uint32_t), &allocated);
        candidate.phi_targets = ir_allocate(output, phi_count, sizeof(uint32_t), &allocated);
        allocated = allocated && dataflow_create(output, function->block_count, values, cfg.edge_count, &candidate.flow);
        check.error = allocated ? IR_OK : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        ir_dataflow_dense(cross, &candidate, BUSTER_VECTOR_PATH(vector));
        memcpy(candidate.flow.successor_offsets, cfg.successor_offsets, ((uint64_t)function->block_count + 1) * sizeof(uint32_t));
        if (cfg.edge_count != 0)
        {
            memcpy(candidate.flow.successors, cfg.successors, (uint64_t)cfg.edge_count * sizeof(uint32_t));
        }
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            candidate.phi_offsets[block + 1] += candidate.phi_offsets[block];
            cursor[block] = candidate.phi_offsets[block];
        }
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            for (uint32_t node = function->block_offsets[block]; node < function->block_offsets[block + 1] && function->op[node] == IR_PHI; node += 1)
            {
                for (uint32_t first = 0; first < function->b[node];)
                {
                    const uint32_t count = function->b[node] - first < 16 ? function->b[node] - first : 16;
                    uint32_t blocks[16];
                    uint32_t values[16];
                    ir_dataflow_phi_batch(function->extra + function->a[node] + (uint64_t)first * 2, count, blocks, values, BUSTER_VECTOR_PATH(vector));
                    for (uint32_t lane = 0; lane < count; lane += 1)
                    {
                        const uint32_t place = cursor[blocks[lane]]++;
                        candidate.phi_values[place] = values[lane];
                        candidate.phi_targets[place] = block;
                    }
                    first += count;
                }
            }
        }
        // No row-pointer arithmetic when every value is local: the four matrix pointers are then null.
        if (candidate.flow.value_count != 0)
        {
            ir_dataflow_ordinary(function, &cfg, NULL, &candidate, BUSTER_VECTOR_PATH(vector));
            for (uint32_t block = 0; block < function->block_count; block += 1)
            {
                uint64_t* def = candidate.flow.def + (uint64_t)block * candidate.flow.row_words;
                const uint32_t end = function->block_offsets[block + 1];
                for (uint32_t first = function->block_offsets[block]; first < end;)
                {
                    const uint32_t count = end - first < 16 ? end - first : 16;
                    ir_dataflow_set(def, candidate.ids + first, count, BUSTER_VECTOR_PATH(vector));
                    first += count;
                }
                for (uint32_t first = candidate.phi_offsets[block]; first < candidate.phi_offsets[block + 1];)
                {
                    const uint32_t count = candidate.phi_offsets[block + 1] - first < 16 ? candidate.phi_offsets[block + 1] - first : 16;
                    uint32_t* values = candidate.phi_values + first;
                    // Still original IDs here. Add only the inputs not defined in this predecessor to use.
                    ir_dataflow_operands(values, count, (1u << count) - 1, block, &cfg, NULL, &candidate, BUSTER_VECTOR_PATH(vector));
                    if (BUSTER_VECTOR_PATH(vector))
                    {
                        const __mmask16 tail = (__mmask16)((1u << count) - 1);
                        const __m512i original = _mm512_maskz_loadu_epi32(tail, values);
                        const __m512i ids = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), tail, original, candidate.ids, 4);
                        _mm512_mask_storeu_epi32(values, tail, ids);
                    }
                    else
                    {
                        for (uint32_t i = 0; i < count; i += 1)
                        {
                            values[i] = candidate.ids[values[i]];
                        }
                    }
                    first += count;
                }
            }
        }
        check.error = dataflow_postorder(&candidate.flow, scratch) ? IR_OK : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        candidate.arena_bytes = output->length - output_mark;
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

static __attribute__((unused)) IrCheck ir_dataflow_prepare(const IrFunction* function, Arena* output, Arena* scratch, IrDataflow* result)
{
    return ir_dataflow_prepare_path(function, output, scratch, result, BUSTER_VECTOR_PATH(true));
}

static __attribute__((unused)) IrCheck ir_dataflow_prepare_scalar(const IrFunction* function, Arena* output, Arena* scratch, IrDataflow* result)
{
    return ir_dataflow_prepare_path(function, output, scratch, result, false);
}

static uint64_t ir_dataflow_solve_path(IrDataflow* dataflow, bool vector)
{
    (void)vector;
    const uint64_t iterations = BUSTER_VECTOR_PATH(vector) ? dataflow_liveness_vector(&dataflow->flow) : dataflow_liveness_scalar(&dataflow->flow);
    for (uint32_t block = 0; block < dataflow->flow.block_count; block += 1)
    {
        for (uint32_t first = dataflow->phi_offsets[block]; first < dataflow->phi_offsets[block + 1];)
        {
            const uint32_t count = dataflow->phi_offsets[block + 1] - first < 16 ? dataflow->phi_offsets[block + 1] - first : 16;
            ir_dataflow_set(dataflow->flow.out + (uint64_t)block * dataflow->flow.row_words, dataflow->phi_values + first, count, BUSTER_VECTOR_PATH(vector));
            first += count;
        }
    }
    return iterations;
}

static __attribute__((unused)) uint64_t ir_dataflow_solve(IrDataflow* dataflow)
{
    return ir_dataflow_solve_path(dataflow, BUSTER_VECTOR_PATH(true));
}

static __attribute__((unused)) uint64_t ir_dataflow_solve_scalar(IrDataflow* dataflow)
{
    return ir_dataflow_solve_path(dataflow, false);
}

#endif
