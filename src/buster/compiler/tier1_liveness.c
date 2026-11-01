// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Selected-use liveness for Tier 1 (#143): Tier1SelectPlan -> dense inter-block Dataflow -> Tier1AllocationInput.
// Include after lane.c, ir.c, dataflow.c, tier1_select.c and tier1_allocate.c. The selector's actual folded-use CSR
// is the source of uses, never the original IR operands. Covered nodes have no interval. Values used only inside
// their defining block have local ranges; only cross-block values enter aligned bitsets. The existing scalar and
// AVX-512 fixpoints solve those rows, and live block boundaries expand conservative hole-free physical-order ranges.
// Uses are at 2*node, definitions at 2*node+1. Parameters start at zero in their incoming SysV register, because
// the emitter emits no parameter move. Bounded direct-call arguments retain their exact ordered uses, including
// duplicates; call results are ordinary definitions. Call-clobber preservation belongs to the native consumer.
// Phi/indirect-call/floating-point plans remain unsupported; no fake edge moves.
// The result retains interval columns and sorted per-interval use CSR only. CFG, dense IDs and bitsets are scratch.
// Both arenas are transactional; input storage is immutable and disjoint from their unused capacity. See
// docs/tier1-liveness.md for limits, grouping, tail rules and the source/corpus acceptance still outstanding.

#ifndef BUSTER_TIER1_LIVENESS_C
#define BUSTER_TIER1_LIVENESS_C

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_TIER1_LIVENESS_MAX_MATRIX_BYTES (64ull << 20)

typedef enum Tier1LivenessError
{
    TIER1_LIVENESS_OK,
    TIER1_LIVENESS_INVALID,
    TIER1_LIVENESS_UNSUPPORTED,
    TIER1_LIVENESS_LIMIT,
    TIER1_LIVENESS_MEMORY,
} Tier1LivenessError;

typedef struct Tier1Liveness Tier1Liveness;
struct Tier1Liveness
{
    Tier1AllocationInput allocation;
    uint32_t cross_block_values;
    uint64_t iterations;
    uint64_t retained_bytes;
    // Upper bound includes dataflow_postorder's bounded visit stack and alignment gaps.
    uint64_t scratch_bytes;
    uint64_t matrix_bytes;
};

static uint32_t tier1_liveness_register_mask(void)
{
    uint32_t mask = 0;
    for (uint8_t reg = 0; reg < 16; reg += 1)
    {
        mask |= tier1_select_register_allowed(reg) ? 1u << reg : 0;
    }
    return mask;
}

static uint32_t tier1_liveness_parameter_mask(uint32_t parameter)
{
    static const uint8_t registers[] = {
        X86_REGISTER_RDI,
        X86_REGISTER_RSI,
        X86_REGISTER_RDX,
        X86_REGISTER_RCX,
        X86_REGISTER_R8,
        X86_REGISTER_R9,
    };
    return parameter < sizeof(registers) ? 1u << registers[parameter] : 0;
}

// With `phis`, for the CFG consumer (tier1_emit_cfg.c), a selected phi is valid too, and so are a parameter after other
// instructions and a narrow one (which it zero-extends at the entry); this file's intervals still refuse them.
static Tier1LivenessError tier1_liveness_validate_plan(const Tier1SelectPlan* plan, Arena* scratch, bool vector, bool phis)
{
    (void)vector;

    const IrFunction* function = plan->function;
    const uint32_t count = function->instruction_count;
    Tier1LivenessError error = TIER1_LIVENESS_OK;
    if (count > BUSTER_TIER1_ALLOCATE_MAX_INTERVALS || count > BUSTER_TIER1_ALLOCATE_MAX_POSITIONS / 2 || plan->effective_use_count > BUSTER_TIER1_ALLOCATE_MAX_USES)
    {
        error = TIER1_LIVENESS_LIMIT;
    }
    else if (!plan->effective_use_offsets || (count != 0 && (!plan->covered || !plan->patterns)) || (plan->effective_use_count != 0 && !plan->effective_use_values))
    {
        error = TIER1_LIVENESS_INVALID;
    }
    else
    {
        // Dominance was already checked by the successful selector plan; this validates the borrowed IR's shape and
        // local rules without fabricating another dominance view. Mutation invalidates both the plan and this API.
        const IrCheck check = BUSTER_VECTOR_PATH(vector) ? ir_verify(function, scratch, 0) : ir_verify_scalar(function, scratch, 0);
        if (check.error != IR_OK)
        {
            error = check.error == IR_ERROR_MEMORY ? TIER1_LIVENESS_MEMORY : TIER1_LIVENESS_INVALID;
        }
    }
    if (error == TIER1_LIVENESS_OK && (plan->effective_use_offsets[0] != 0 || plan->effective_use_offsets[count] != plan->effective_use_count))
    {
        error = TIER1_LIVENESS_INVALID;
    }
    bool parameter_prefix = true;
    for (uint32_t node = 0; error == TIER1_LIVENESS_OK && node < count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t type = function->type[node];
        const uint32_t first = plan->effective_use_offsets[node];
        const uint32_t end = plan->effective_use_offsets[node + 1];
        if (plan->covered[node] > 1 || first > end || end > plan->effective_use_count || (plan->covered[node] && first != end))
        {
            error = TIER1_LIVENESS_INVALID;
        }
        else if ((op == IR_PHI && !phis) || (op == IR_CALL_INDIRECT && !phis) || ((op == IR_CALL || op == IR_CALL_INDIRECT) && !tier1_select_call_supported(function, node)) ||
                 (type != IR_VOID && tier1_select_width(type) == 0 && !(phis && tier1_select_mask_width(type) != 0)))
        {
            // A mask (#179) is the CFG consumer's alone, whose allocation has the opmask class.
            error = TIER1_LIVENESS_UNSUPPORTED;
        }
        else if (op == IR_PHI && (plan->covered[node] || plan->patterns[node].rule != TIER1_RULE_PHI || first != end))
        {
            error = TIER1_LIVENESS_INVALID;
        }
        else if (op == IR_CALL || op == IR_CALL_INDIRECT)
        {
            // An indirect call's callee is its last effective use, after its arguments.
            const IrCall call = function->calls[function->b[node]];
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            const uint32_t indirect = op == IR_CALL_INDIRECT;
            if (plan->covered[node] || pattern->rule != TIER1_RULE_CALL || pattern->immediate != function->a[node] || pattern->width != tier1_select_width(type) || pattern->fold_count != 0 ||
                end - first != call.argument_count + indirect || (indirect && plan->effective_use_values[first + call.argument_count] != function->a[node]))
            {
                error = TIER1_LIVENESS_INVALID;
            }
            for (uint32_t operand = 0; error == TIER1_LIVENESS_OK && operand < 3; operand += 1)
            {
                if (pattern->values[operand] != BUSTER_IR_NONE)
                {
                    error = TIER1_LIVENESS_INVALID;
                }
            }
            for (uint32_t argument = 0; error == TIER1_LIVENESS_OK && argument < call.argument_count; argument += 1)
            {
                if (plan->effective_use_values[first + argument] != function->extra[call.argument_first + argument])
                {
                    error = TIER1_LIVENESS_INVALID;
                }
            }
        }
        else if (op == IR_UNREACHABLE)
        {
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            if (plan->covered[node] || pattern->rule != TIER1_RULE_UNREACHABLE || pattern->width != 0 || pattern->fold_count != 0 || first != end)
            {
                error = TIER1_LIVENESS_INVALID;
            }
            for (uint32_t operand = 0; error == TIER1_LIVENESS_OK && operand < 3; operand += 1)
            {
                if (pattern->values[operand] != BUSTER_IR_NONE)
                {
                    error = TIER1_LIVENESS_INVALID;
                }
            }
        }
        else if (plan->patterns[node].rule == TIER1_RULE_CALL || plan->patterns[node].rule == TIER1_RULE_UNREACHABLE)
        {
            error = TIER1_LIVENESS_INVALID;
        }
        else if (!phis && !plan->covered[node] && (plan->patterns[node].rule == TIER1_RULE_DIVIDE || plan->patterns[node].rule == TIER1_RULE_SHIFT))
        {
            // A division's or shift's by a register is the CFG consumer's (tier1_emit_cfg_chain); a division by a constant's
            // reciprocal takes its fixed registers in either consumer.
            error = TIER1_LIVENESS_UNSUPPORTED;
        }
        else if (op == IR_PARAM && (!parameter_prefix || plan->covered[node] || tier1_liveness_parameter_mask(function->a[node]) == 0 ||
                                    (type != IR_I32 && type != IR_I64 && type != IR_PTR && !(phis && (type == IR_I1 || type == IR_I8 || type == IR_I16)))))
        {
            error = TIER1_LIVENESS_UNSUPPORTED;
        }
        // The CFG consumer defines every parameter at the function's start, so anything may come first.
        parameter_prefix &= op == IR_PARAM || phis;
        for (uint32_t use = first; error == TIER1_LIVENESS_OK && use < end; use += 1)
        {
            const uint32_t value = plan->effective_use_values[use];
            // The selector's current subset has definitions before uses even across block boundaries.
            if (value >= node || plan->covered[value] || function->type[value] == IR_VOID)
            {
                error = TIER1_LIVENESS_INVALID;
            }
        }
    }
    return error;
}

static uint32_t tier1_liveness_dense(const uint8_t* cross, uint32_t count, uint32_t* ids, uint32_t* values, bool vector)
{
    (void)vector;

    uint32_t dense = 0;
    if (count != 0)
    {
        memset(ids, 0xff, (uint64_t)count * sizeof(ids[0]));
    }
    if (BUSTER_VECTOR_PATH(vector))
    {
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        const __m512i prefixes = _mm512_setr_epi32(0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767);
        for (uint32_t first = 0; first < count; first += 16)
        {
            const uint32_t length = count - first < 16 ? count - first : 16;
            const __mmask16 active = (__mmask16)((1u << length) - 1);
            const __m128i bytes = _mm_maskz_loadu_epi8(active, cross + first);
            const __mmask16 selected = _mm_cmpneq_epu8_mask(bytes, _mm_setzero_si128()) & active;
            const __m512i positions = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)first));
            const __m512i rank = _mm512_popcnt_epi32(_mm512_and_si512(prefixes, _mm512_set1_epi32(selected)));
            _mm512_mask_compressstoreu_epi32(values + dense, selected, positions);
            _mm512_mask_storeu_epi32(ids + first, selected, _mm512_add_epi32(rank, _mm512_set1_epi32((int)dense)));
            dense += (uint32_t)__builtin_popcount((uint32_t)selected);
        }
    }
    else
    {
        for (uint32_t value = 0; value < count; value += 1)
        {
            if (cross[value])
            {
                ids[value] = dense;
                values[dense] = value;
                dense += 1;
            }
        }
    }
    return dense;
}

// Up to 16 already resolved dense IDs; NONE lanes are inactive. Combine collisions before scalar word stores,
// rather than scattering or losing duplicate-word updates. A row has whole 512-bit vectors, initialized to zero.
static void tier1_liveness_set_row(uint64_t* row, const uint32_t* ids, uint32_t count, bool vector)
{
    (void)vector;

    if (BUSTER_VECTOR_PATH(vector))
    {
        const __mmask16 tail = (__mmask16)((1u << count) - 1);
        const __m512i values = _mm512_maskz_loadu_epi32(tail, ids);
        const __mmask16 active = tail & _mm512_cmpneq_epu32_mask(values, _mm512_set1_epi32(-1));
        const __m512i words = _mm512_srli_epi32(values, 5);
        const __m512i shifts = _mm512_and_si512(values, _mm512_set1_epi32(31));
        const __m512i bits = _mm512_sllv_epi32(_mm512_set1_epi32(1), shifts);
        const __m512i previous = _mm512_conflict_epi32(words);
        // Inactive trailing lanes cannot hide an earlier active first occurrence; inactive NONE lanes have a word
        // index outside every valid row. Thus ordinary conflict detection is sufficient without a masked gather.
        uint32_t leaders = active & _mm512_cmpeq_epi32_mask(previous, _mm512_setzero_si512());
        uint32_t word_ids[16];
        _mm512_storeu_si512(word_ids, words);
        uint32_t* row32 = (uint32_t*)row;
        while (leaders != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(leaders);
            const uint32_t word = word_ids[lane];
            const __mmask16 group = active & _mm512_cmpeq_epi32_mask(words, _mm512_set1_epi32((int)word));
            row32[word] |= _mm512_mask_reduce_or_epi32(group, bits);
            leaders &= leaders - 1;
        }
    }
    else
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (ids[i] != BUSTER_IR_NONE)
            {
                row[ids[i] / 64] |= UINT64_C(1) << (ids[i] & 63);
            }
        }
    }
}

static void tier1_liveness_rows(const Tier1SelectPlan* plan, const IrCfg* cfg, const uint32_t* dense_ids, Dataflow* dataflow, bool vector)
{
    (void)vector;

    const IrFunction* function = plan->function;
    if (dataflow->row_words != 0)
    {
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            uint64_t* use = dataflow->use + (uint64_t)block * dataflow->row_words;
            uint64_t* def = dataflow->def + (uint64_t)block * dataflow->row_words;
            const uint32_t first_node = function->block_offsets[block];
            const uint32_t end_node = function->block_offsets[block + 1];
            const uint32_t end_use = plan->effective_use_offsets[end_node];
            for (uint32_t first = plan->effective_use_offsets[first_node]; first < end_use; first += 16)
            {
                const uint32_t count = end_use - first < 16 ? end_use - first : 16;
                uint32_t ids[16];
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    const uint32_t value = plan->effective_use_values[first + lane];
                    ids[lane] = cfg->instruction_blocks[value] != block || function->op[value] == IR_PARAM ? dense_ids[value] : BUSTER_IR_NONE;
                }
                tier1_liveness_set_row(use, ids, count, BUSTER_VECTOR_PATH(vector));
            }
            for (uint32_t first = first_node; first < end_node; first += 16)
            {
                const uint32_t count = end_node - first < 16 ? end_node - first : 16;
                uint32_t ids[16];
                for (uint32_t lane = 0; lane < count; lane += 1)
                {
                    // PARAM emits no definition on an entry backedge: the original incoming value must survive.
                    ids[lane] = function->op[first + lane] == IR_PARAM ? BUSTER_IR_NONE : dense_ids[first + lane];
                }
                tier1_liveness_set_row(def, ids, count, BUSTER_VECTOR_PATH(vector));
            }
        }
    }
}

static void tier1_liveness_envelopes(const IrFunction* function, const Dataflow* dataflow, const uint32_t* dense_values, const uint32_t* intervals, uint32_t* starts, uint32_t* ends)
{
    for (uint32_t block = 0; dataflow->row_words != 0 && block < function->block_count; block += 1)
    {
        const uint32_t first = function->block_offsets[block] * 2;
        const uint32_t end = function->block_offsets[block + 1] * 2;
        const uint64_t* in = dataflow->in + (uint64_t)block * dataflow->row_words;
        const uint64_t* out = dataflow->out + (uint64_t)block * dataflow->row_words;
        const uint32_t words = (dataflow->value_count + 63) / 64;
        for (uint32_t word = 0; word < words; word += 1)
        {
            uint64_t live = in[word] | out[word];
            while (live != 0)
            {
                const uint32_t bit = (uint32_t)__builtin_ctzll(live);
                const uint32_t interval = intervals[dense_values[word * 64 + bit]];
                if ((in[word] >> bit) & 1)
                {
                    starts[interval] = starts[interval] < first ? starts[interval] : first;
                }
                if ((out[word] >> bit) & 1)
                {
                    ends[interval] = ends[interval] > end ? ends[interval] : end;
                }
                live &= live - 1;
            }
        }
    }
}

static __attribute__((unused)) Tier1LivenessError tier1_liveness_build(const Tier1SelectPlan* plan, Arena* output, Arena* scratch, bool vector, Tier1Liveness* result)
{
    (void)vector;

    const bool arguments = plan && plan->function && output && scratch && output != scratch && output->base != scratch->base && result;
    Tier1LivenessError error = arguments ? TIER1_LIVENESS_OK : TIER1_LIVENESS_INVALID;
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    const IrFunction* function = plan ? plan->function : 0;
    const uint32_t count = function ? function->instruction_count : 0;
    Tier1Liveness live = { 0 };
    IrCfg cfg = { 0 };
    if (error == TIER1_LIVENESS_OK)
    {
        error = tier1_liveness_validate_plan(plan, scratch, BUSTER_VECTOR_PATH(vector), false);
    }
    if (error == TIER1_LIVENESS_OK)
    {
        const IrError cfg_error = ir_build_cfg(function, scratch, output, &cfg);
        if (cfg_error != IR_OK)
        {
            error = cfg_error == IR_ERROR_MEMORY ? TIER1_LIVENESS_MEMORY : TIER1_LIVENESS_INVALID;
        }
    }
    uint32_t* intervals = 0;
    uint32_t* dense_ids = 0;
    uint32_t* dense_values = 0;
    uint8_t* cross = 0;
    if (error == TIER1_LIVENESS_OK)
    {
        bool ok = true;
        intervals = ir_allocate(scratch, count, 4, &ok);
        dense_ids = ir_allocate(scratch, count, 4, &ok);
        dense_values = ir_allocate(scratch, count, 4, &ok);
        cross = ir_allocate(scratch, count, 1, &ok);
        if (!ok)
        {
            error = TIER1_LIVENESS_MEMORY;
        }
    }
    uint32_t interval_count = 0;
    for (uint32_t node = 0; error == TIER1_LIVENESS_OK && node < count; node += 1)
    {
        const bool definition = !plan->covered[node] && function->type[node] != IR_VOID;
        intervals[node] = definition ? interval_count++ : BUSTER_IR_NONE;
        for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
        {
            const uint32_t value = plan->effective_use_values[use];
            cross[value] |= cfg.instruction_blocks[value] != cfg.instruction_blocks[node];
            cross[value] |= function->op[value] == IR_PARAM && cfg.predecessor_offsets[1] != 0;
        }
    }
    uint32_t* starts = 0;
    uint32_t* ends = 0;
    uint32_t* values = 0;
    uint32_t* allowed = 0;
    uint8_t* classes = 0;
    uint32_t* use_offsets = 0;
    uint32_t* use_positions = 0;
    uint32_t* cursors = 0;
    Dataflow dataflow = { 0 };
    if (error == TIER1_LIVENESS_OK)
    {
        live.cross_block_values = tier1_liveness_dense(cross, count, dense_ids, dense_values, BUSTER_VECTOR_PATH(vector));
        const uint64_t row_words = ((uint64_t)live.cross_block_values + 511) / 512 * 8;
        live.matrix_bytes = 4 * (uint64_t)function->block_count * row_words * sizeof(uint64_t);
        if (live.matrix_bytes > BUSTER_TIER1_LIVENESS_MAX_MATRIX_BYTES)
        {
            error = TIER1_LIVENESS_LIMIT;
        }
    }
    if (error == TIER1_LIVENESS_OK)
    {
        bool ok = true;
        starts = ir_allocate(output, interval_count, 4, &ok);
        ends = ir_allocate(output, interval_count, 4, &ok);
        values = ir_allocate(output, interval_count, 4, &ok);
        allowed = ir_allocate(output, interval_count, 4, &ok);
        classes = ir_allocate(output, interval_count, 1, &ok);
        use_offsets = ir_allocate(output, (uint64_t)interval_count + 1, 4, &ok);
        use_positions = ir_allocate(output, plan->effective_use_count, 4, &ok);
        cursors = ir_allocate(scratch, interval_count, 4, &ok);
        if (!ok || !dataflow_create(scratch, function->block_count, live.cross_block_values, cfg.edge_count, &dataflow))
        {
            error = TIER1_LIVENESS_MEMORY;
        }
    }
    if (error == TIER1_LIVENESS_OK)
    {
        const uint32_t registers = tier1_liveness_register_mask();
        for (uint32_t node = 0; node < count; node += 1)
        {
            if (intervals[node] != BUSTER_IR_NONE)
            {
                const uint32_t interval = intervals[node];
                const bool parameter = function->op[node] == IR_PARAM;
                starts[interval] = parameter ? 0 : node * 2 + 1;
                ends[interval] = starts[interval] + 1;
                values[interval] = node;
                allowed[interval] = parameter ? tier1_liveness_parameter_mask(function->a[node]) : registers;
                classes[interval] = BUSTER_TIER1_ALLOCATE_GPR;
            }
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t interval = intervals[plan->effective_use_values[use]];
                const uint32_t end = node * 2 + 1;
                use_offsets[interval + 1] += 1;
                ends[interval] = ends[interval] > end ? ends[interval] : end;
            }
        }
        for (uint32_t interval = 0; interval < interval_count; interval += 1)
        {
            use_offsets[interval + 1] += use_offsets[interval];
            cursors[interval] = use_offsets[interval];
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            for (uint32_t use = plan->effective_use_offsets[node]; use < plan->effective_use_offsets[node + 1]; use += 1)
            {
                const uint32_t interval = intervals[plan->effective_use_values[use]];
                use_positions[cursors[interval]] = node * 2;
                cursors[interval] += 1;
            }
        }
        memcpy(dataflow.successor_offsets, cfg.successor_offsets, ((uint64_t)cfg.block_count + 1) * sizeof(uint32_t));
        if (cfg.edge_count != 0)
        {
            memcpy(dataflow.successors, cfg.successors, (uint64_t)cfg.edge_count * sizeof(uint32_t));
        }
        tier1_liveness_rows(plan, &cfg, dense_ids, &dataflow, BUSTER_VECTOR_PATH(vector));
        live.scratch_bytes = scratch->length - scratch_mark + (uint64_t)function->block_count * (sizeof(DataflowVisit) + 1) + 2 * _Alignof(DataflowVisit);
        if (!dataflow_postorder(&dataflow, scratch))
        {
            error = TIER1_LIVENESS_MEMORY;
        }
        else
        {
            live.iterations = BUSTER_VECTOR_PATH(vector) ? dataflow_liveness_vector(&dataflow) : dataflow_liveness_scalar(&dataflow);
            tier1_liveness_envelopes(function, &dataflow, dense_values, intervals, starts, ends);
            live.allocation = (Tier1AllocationInput){
                .starts = starts,
                .ends = ends,
                .values = values,
                .allowed_registers = allowed,
                .classes = classes,
                .use_offsets = use_offsets,
                .use_positions = use_positions,
                .use_count = plan->effective_use_count,
                .count = interval_count,
                .value_count = count,
                .position_count = count * 2,
                .registers = { registers, 0 },
            };
            live.retained_bytes = output->length - output_mark;
            if (!tier1_allocation_input_valid(&live.allocation))
            {
                error = TIER1_LIVENESS_INVALID;
            }
        }
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (error == TIER1_LIVENESS_OK)
        {
            *result = live;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
    }
    return error;
}

#endif
