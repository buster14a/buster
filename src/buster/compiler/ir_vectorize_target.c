// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Which vector operations a target selects (#2743), for the loop vectorizer (ir_vectorize_loops.c): Tier 1's own forms
// at each width and encoding (tier1_select.c's tables and ir_simd_convert_forms.c's), checked against the target's
// allow-list, so that the vectorizer emits no row whose emission the target would refuse (a refusal sends the whole
// function to Tier 0). Include after tier1_select.c and ir_vectorize_loops.c.

#ifndef BUSTER_IR_VECTORIZE_TARGET_C
#define BUSTER_IR_VECTORIZE_TARGET_C

static bool ir_vectorize_target_allows(const uint8_t* allowed, uint32_t form)
{
    const bool result = form != X86_FORM_NONE && x86_target_form_allowed(allowed, form);
    return result;
}

// Tier 1's operations at the target whose forms `allowed` lists: each kind's forms as tier1_select gives them at the
// target's encoding (EVEX with AVX512VL, VEX with AVX, else legacy SSE at 16 bytes; 64 bytes with AVX512F).
static __attribute__((unused)) void ir_vectorize_target_tier1(const uint8_t* allowed, IrVectorizeTarget* target)
{
    *target = (IrVectorizeTarget){ 0 };
    const bool avx = ir_vectorize_target_allows(allowed, X86_FORM_VMOVDQU_X_XM128);
    const bool vl = ir_vectorize_target_allows(allowed, X86_FORM_VMOVDQA64_X_XM128);
    const bool zmm = ir_vectorize_target_allows(allowed, X86_FORM_VMOVDQA64_Z_ZM512);
    const uint32_t encoding = !avx ? BUSTER_TIER1_ENCODING_SSE : !vl ? BUSTER_TIER1_ENCODING_VEX : BUSTER_TIER1_ENCODING_EVEX;
    target->moves = (uint8_t)(1u | (avx ? 2u : 0u) | (zmm ? 4u : 0u));
    static const uint8_t operations[][2] = {
        { IR_ADD, BUSTER_TIER1_VECTOR_ADD },   { IR_SUB, BUSTER_TIER1_VECTOR_SUB },   { IR_MUL, BUSTER_TIER1_VECTOR_MUL },   { IR_AND, BUSTER_TIER1_VECTOR_AND },
        { IR_OR, BUSTER_TIER1_VECTOR_OR },     { IR_XOR, BUSTER_TIER1_VECTOR_XOR },   { IR_SHL, BUSTER_TIER1_VECTOR_SHL },   { IR_LSHR, BUSTER_TIER1_VECTOR_LSHR },
        { IR_ASHR, BUSTER_TIER1_VECTOR_ASHR }, { IR_FADD, BUSTER_TIER1_VECTOR_FADD }, { IR_FSUB, BUSTER_TIER1_VECTOR_FSUB }, { IR_FMUL, BUSTER_TIER1_VECTOR_FMUL },
        { IR_FDIV, BUSTER_TIER1_VECTOR_FDIV },
    };
    static const uint8_t splat_types[IR_SIMD_LANE_COUNT] = {
        [IR_SIMD_LANE_I32] = IR_I32,
        [IR_SIMD_LANE_I8] = IR_I8,
        [IR_SIMD_LANE_I16] = IR_I16,
        [IR_SIMD_LANE_I64] = IR_I64,
        [IR_SIMD_LANE_F32] = IR_F32,
        [IR_SIMD_LANE_F64] = IR_F64,
    };
    static const uint16_t broadcasts[IR_TYPE_COUNT][3] = {
        [IR_I8] = { X86_FORM_VPBROADCASTB_X_R32, X86_FORM_VPBROADCASTB_Y_R32, X86_FORM_VPBROADCASTB_Z_R32 },
        [IR_I16] = { X86_FORM_VPBROADCASTW_X_R32, X86_FORM_VPBROADCASTW_Y_R32, X86_FORM_VPBROADCASTW_Z_R32 },
        [IR_I32] = { X86_FORM_VPBROADCASTD_X_R32, X86_FORM_VPBROADCASTD_Y_R32, X86_FORM_VPBROADCASTD_Z_R32 },
        [IR_I64] = { X86_FORM_VPBROADCASTQ_X_R64, X86_FORM_VPBROADCASTQ_Y_R64, X86_FORM_VPBROADCASTQ_Z_R64 },
        [IR_F32] = { X86_FORM_VBROADCASTSS_X_XM32_EVEX, X86_FORM_VBROADCASTSS_Y_XM32_EVEX, X86_FORM_VBROADCASTSS_Z_XM32 },
        [IR_F64] = { X86_FORM_VPBROADCASTQ_X_XM64_EVEX, X86_FORM_VBROADCASTSD_Y_XM64_EVEX, X86_FORM_VBROADCASTSD_Z_XM64 },
    };
    for (uint32_t w = 0; w < BUSTER_IR_VECTORIZE_WIDTHS; w += 1)
    {
        const uint32_t width = 16u << w;
        const uint8_t bit = (uint8_t)(1u << w);
        const bool moves = ((target->moves >> w) & 1) != 0;
        const bool legacy = width < 64 && encoding != BUSTER_TIER1_ENCODING_EVEX;
        for (uint32_t lane = 0; moves && lane < IR_SIMD_LANE_COUNT; lane += 1)
        {
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operations); k += 1)
            {
                const uint32_t op = operations[k][0];
                const bool bitwise = op == IR_AND || op == IR_OR || op == IR_XOR;
                const uint16_t form = tier1_select_lane_form(operations[k][1], bitwise ? IR_SIMD_LANE_I32 : lane, width, encoding);
                target->binary[op][lane] |= ir_vectorize_target_allows(allowed, form) ? bit : 0;
            }
            // A comparison into a mask and a blend under one are EVEX's alone.
            const bool floating = lane == IR_SIMD_LANE_F32 || lane == IR_SIMD_LANE_F64;
            const bool compare = ir_vectorize_target_allows(allowed, tier1_select_lane_compares[lane][0][w]) && (floating || ir_vectorize_target_allows(allowed, tier1_select_lane_compares[lane][1][w]));
            target->compare[lane] |= compare && !legacy ? bit : 0;
            target->select[lane] |= ir_vectorize_target_allows(allowed, tier1_select_lane_blends[lane][w]) && !legacy ? bit : 0;
            // A splat: EVEX's broadcast, or the moves, unpacks and shuffle of SSE and VEX (tier1_select_scalar_splat).
            const uint32_t type = splat_types[lane];
            bool splat = false;
            if (legacy)
            {
                const bool vex = encoding == BUSTER_TIER1_ENCODING_VEX;
                const uint32_t bytes = ir_type_bits[type] / 8;
                const bool float_type = type == IR_F32 || type == IR_F64;
                const uint16_t move = bytes == 8 ? (vex ? X86_FORM_VMOVQ_X_RM64 : X86_FORM_MOVQ_X_RM64) : (vex ? X86_FORM_VMOVD_X_RM32 : X86_FORM_MOVD_X_RM32);
                splat = (float_type || ir_vectorize_target_allows(allowed, move)) && ir_vectorize_target_allows(allowed, vex ? X86_FORM_VPSHUFD_X_XM128_IMM8 : X86_FORM_PSHUFD_X_XM128_IMM8);
                splat = splat && (bytes != 1 || ir_vectorize_target_allows(allowed, vex ? X86_FORM_VPUNPCKLBW_X_X_XM128 : X86_FORM_PUNPCKLBW_X_XM128));
                splat = splat && (bytes > 2 || ir_vectorize_target_allows(allowed, vex ? X86_FORM_VPUNPCKLWD_X_X_XM128 : X86_FORM_PUNPCKLWD_X_XM128));
                splat = splat && (width != 32 || ir_vectorize_target_allows(allowed, X86_FORM_VINSERTF128_Y_Y_XM128_IMM8));
            }
            else
            {
                splat = ir_vectorize_target_allows(allowed, broadcasts[type][w]);
            }
            target->splat[lane] |= splat ? bit : 0;
        }
        for (uint32_t from = 0; moves && from < 4; from += 1)
        {
            for (uint32_t to = 0; to < 4; to += 1)
            {
                // An extension at its result's width, its source the narrower vector it reads the low lanes of; a
                // truncation at its source's width, its result 16 bytes or more.
                for (uint32_t mode = 0; mode < 2 && from < to; mode += 1)
                {
                    const uint32_t source = width >> (to - from);
                    const uint32_t flags = from | to << 2 | mode << 4;
                    const uint16_t form = tier1_select_convert_form(0, flags, width, source < 16 ? 16 : source);
                    target->extend[from][to][mode] |= !legacy && ir_vectorize_target_allows(allowed, form) ? bit : 0;
                }
                if (from > to)
                {
                    const uint32_t result = width >> (from - to);
                    const uint16_t form = tier1_select_convert_form(1, from | to << 2, result < 16 ? 16 : result, width);
                    target->truncate[from][to] |= !legacy && ir_vectorize_target_allows(allowed, form) ? bit : 0;
                }
            }
            target->popcount[from] |= !legacy && ir_vectorize_target_allows(allowed, ir_simd_lane_unary_forms[BUSTER_IR_LANE_UNARY_POPCOUNT][from][w]) ? bit : 0;
        }
        // A reduction: tier1_select_reduce's halvings down to one lane, each with its combining form, then lane 0's move.
        for (uint32_t size = 2; moves && size < 4; size += 1)
        {
            for (uint32_t operation = BUSTER_IR_REDUCE_ADD; operation < BUSTER_IR_REDUCE_OPERATIONS; operation += 1)
            {
                bool legal = !legacy && ir_vectorize_target_allows(allowed, size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX);
                for (uint32_t span = width / 2; legal && span >= (1u << size); span /= 2)
                {
                    const uint16_t halve = span >= 16 ? (span == 32 ? X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8 : X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8) : X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX;
                    legal = ir_vectorize_target_allows(allowed, halve) && ir_vectorize_target_allows(allowed, ir_simd_reduce_form(operation, size, span == 32 ? 1 : 0));
                }
                target->reduce[size][operation] |= legal ? bit : 0;
            }
        }
    }
}

#endif
