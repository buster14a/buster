// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Register exchanges after the CFG allocation (#2610). Include after tier1_emit_cfg_callee_used.
// The assignment's fragments, spills and edge conventions remain the same under one physical-register bijection.
// ABI argument registers and fixed machine roles remain physical; the entry copies renamed parameters into place.

// Rename general-register masks, sixteen at a time. Other classes retain their own masks.
static void tier1_emit_exchange_masks(const uint32_t* source, const uint8_t* classes, uint32_t count, const uint8_t permutation[16], uint32_t* destination)
{
    for (uint32_t i = 0; i < count; i += 16)
    {
        const uint32_t left = count - i;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        const __m512i original = _mm512_maskz_loadu_epi32(lanes, source + i);
        const __mmask16 general = lanes & _mm_cmpeq_epi8_mask(_mm_maskz_loadu_epi8(lanes, classes + i), _mm_setzero_si128());
        __m512i renamed = _mm512_setzero_si512();
        for (uint32_t r = 0; r < 16; r += 1)
        {
            const __m512i bit = _mm512_set1_epi32(1u << r);
            const __mmask16 present = _mm512_test_epi32_mask(original, bit);
            renamed = _mm512_mask_or_epi32(renamed, present, renamed, _mm512_set1_epi32(1u << permutation[r]));
        }
        _mm512_mask_storeu_epi32(destination + i, lanes, _mm512_mask_mov_epi32(original, general, renamed));
    }
}

// The assignment owns its register and move columns. Copy the borrowed masks before changing any owned column, so
// an exhausted arena leaves both input and result unchanged. A successful renaming preserves every fragment's kind
// and extent, every slot, and the already resolved order of every edge and split move. The scratch register is a
// fixed point, so a move cycle still resolves through the same scratch register. This is the emitter's problem:
// definition_registers binds only incoming ABI parameters, and no interval has a static register hint.
static Tier1EmitError tier1_emit_cfg_exchange_map(const uint8_t permutation[16], Arena* output, Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t saved = output->length;
    uint32_t seen = 0;
    uint32_t changed = 0;
    bool valid = true;
    for (uint32_t r = 0; r < 16; r += 1)
    {
        const uint32_t next = permutation[r];
        const uint32_t bit = next < 16 ? 1u << next : 0;
        valid &= bit != 0 && (seen & bit) == 0;
        seen |= bit;
        changed |= next != r ? 1u << r : 0;
    }
    const uint32_t temporary = input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR];
    valid &= (changed & ~input->registers[BUSTER_TIER1_ALLOCATE_GPR]) == 0 &&
             (changed & ~cfg->intervals.registers[BUSTER_TIER1_ALLOCATE_GPR]) == 0 &&
             temporary < 16 && permutation[temporary] == temporary && (changed == 0 || cfg->intervals.hints == 0);
    for (uint32_t r = 0; valid && r < input->fixed.count; r += 1)
    {
        valid &= input->fixed.classes[r] != BUSTER_TIER1_ALLOCATE_GPR || (input->fixed.registers[r] & changed) == 0;
    }
    for (uint32_t s = 0; valid && s < input->sequence_count; s += 1)
    {
        valid &= (input->sequence_registers[s] & changed) == 0;
    }
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    if (valid && changed != 0)
    {
        bool ok = true;
        const uint32_t values = input->value_count;
        const uint32_t intervals = cfg->intervals.count;
        uint32_t* allowed = ir_allocate(output, (uint64_t)values + 1, 4, &ok);
        uint32_t* defined = ok && input->definition_registers != 0 ? ir_allocate(output, (uint64_t)values + 1, 4, &ok) : 0;
        uint32_t* ranges = ok ? ir_allocate(output, (uint64_t)intervals + 1, 4, &ok) : 0;
        error = ok ? error : TIER1_EMIT_MEMORY;
        if (ok)
        {
            tier1_emit_exchange_masks(input->allowed_registers, input->classes, values, permutation, allowed);
            tier1_emit_exchange_masks(cfg->intervals.allowed_registers, cfg->intervals.classes, intervals, permutation, ranges);
            const __m128i table_bytes = _mm_loadu_si128((const __m128i*)permutation);
            const __m512i table = _mm512_broadcast_i32x4(table_bytes);
            const __m512i locations = _mm512_cvtepu8_epi32(table_bytes);
            for (uint32_t v = 0; v < values; v += 16)
            {
                const uint32_t left = values - v;
                const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
                const __mmask16 general = lanes & _mm_cmpeq_epi8_mask(_mm_maskz_loadu_epi8(lanes, input->classes + v), _mm_setzero_si128());
                if (defined != 0)
                {
                    const __m512i original = _mm512_maskz_loadu_epi32(lanes, input->definition_registers + v);
                    const __mmask16 renamed = general & _mm512_test_epi32_mask(original, _mm512_set1_epi32(changed));
                    // These are the ABI-bound parameters. Zero requests the entry's parallel copy; binding the new
                    // physical register instead would incorrectly claim the caller had already supplied it there.
                    _mm512_mask_storeu_epi32(defined + v, lanes, _mm512_mask_mov_epi32(original, renamed, _mm512_setzero_si512()));
                }
                const __m128i original = _mm_maskz_loadu_epi8(lanes, cfg->allocation.value_registers + v);
                const __mmask16 held = general & _mm_cmplt_epu8_mask(original, _mm_set1_epi8(16));
                _mm_mask_storeu_epi8(cfg->allocation.value_registers + v, lanes, _mm_mask_shuffle_epi8(original, held, table_bytes, original));
            }
            for (uint32_t i = 0; i < intervals; i += 1)
            {
                const uint32_t first = cfg->allocation.fragment_offsets[i];
                const uint32_t past = cfg->allocation.fragment_offsets[i + 1];
                for (uint32_t f = first; cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR && f < past; f += 64)
                {
                    const uint32_t left = past - f;
                    const __mmask64 lanes = left >= 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << left) - 1);
                    const __m512i original = _mm512_maskz_loadu_epi8(lanes, cfg->allocation.registers + f);
                    const __mmask64 held = lanes & _mm512_cmplt_epu8_mask(original, _mm512_set1_epi8(16));
                    _mm512_mask_storeu_epi8(cfg->allocation.registers + f, lanes, _mm512_mask_shuffle_epi8(original, held, table, original));
                }
            }
            for (uint32_t m = 0; m < cfg->move_count; m += 16)
            {
                const uint32_t left = cfg->move_count - m;
                const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
                const __m512i from = _mm512_maskz_loadu_epi32(lanes, cfg->move_sources + m);
                const __m512i to = _mm512_maskz_loadu_epi32(lanes, cfg->move_destinations + m);
                const __mmask16 general_from = lanes & _mm512_cmplt_epu32_mask(from, _mm512_set1_epi32(16));
                const __mmask16 general_to = lanes & _mm512_cmplt_epu32_mask(to, _mm512_set1_epi32(16));
                _mm512_mask_storeu_epi32(cfg->move_sources + m, lanes, _mm512_mask_permutexvar_epi32(from, general_from, from, locations));
                _mm512_mask_storeu_epi32(cfg->move_destinations + m, lanes, _mm512_mask_permutexvar_epi32(to, general_to, to, locations));
            }
            input->allowed_registers = allowed;
            input->definition_registers = defined;
            cfg->intervals.allowed_registers = ranges;
            cfg->retained_bytes += output->length - saved;
        }
    }
    if (error != TIER1_EMIT_OK)
    {
        arena_pop_to(output, saved);
    }
    return error;
}

#include "tier1_emit_exchange_cost.c"

static Tier1EmitError tier1_emit_cfg_exchange(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t saved = scratch->length;
    Tier1EmitError error = TIER1_EMIT_OK;
    if (emitted->prepared.call_count != 0 && emitted->callee_registers != 0)
    {
        uint8_t permutation[16];
        const bool ready = tier1_emit_exchange_cost(plan, emitted, input, cfg, scratch, permutation);
        error = ready ? tier1_emit_cfg_exchange_map(permutation, output, input, cfg) : TIER1_EMIT_MEMORY;
    }
    arena_pop_to(scratch, saved);
    return error;
}
