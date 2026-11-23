// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The upper vector state of emitted code (#2830, #3335), shared by Tier 0 and Tier 1: whether records leave it dirty
// (x86_upper_dirty), which a vzeroupper on the way out then clears, and the VEX twins of legacy SSE records
// (x86_vex_forms, x86_vex_record), which a target with AVX runs in their place. A legacy SSE write keeps its
// destination's bits above 127, which the processor makes wait for the register's old value while the upper state is
// dirty; the VEX one doesn't. Include after the x86 encoder (x86_form_descriptions).

#ifndef BUSTER_X86_UPPER_STATE_INCLUDED
#define BUSTER_X86_UPPER_STATE_INCLUDED

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// The VEX twin of each legacy SSE form Tier 1's selection writes (#2830, tier1_emit_vex), for a target with AVX: the same operation on the
// low 128 bits of its registers, zeroing the bits above them where the legacy form keeps them. A legacy write keeps its
// destination's bits above 127, which the processor makes wait for the register's old value while the upper state is
// dirty (6.0 cycles an element against 1.07 in #2755's saxpy body on the 9700X); the VEX one doesn't, and leaves the state
// clean. With BUSTER_X86_VEX_THREE, its first source is VEX.vvvv, the legacy form's destination (addss x, y is vaddss x,
// x, y); without it the twin has the legacy form's operands (vmovaps, vmovd, vucomiss, vcvttss2si, vpshufd). movss and
// movsd, whose legacy form is a load or a register's merge by its mode, are x86_vex_record's.
#define BUSTER_X86_VEX_THREE 0x8000u
#define BUSTER_X86_VEX_FORM 0x7fffu
// X(legacy form, its twin, BUSTER_X86_VEX_THREE or 0).
#define BUSTER_X86_VEX_TWINS(X) \
    X(ADDSS_X_XM32, VADDSS_X_X_XM32, BUSTER_X86_VEX_THREE) \
    X(SUBSS_X_XM32, VSUBSS_X_X_XM32, BUSTER_X86_VEX_THREE) \
    X(MULSS_X_XM32, VMULSS_X_X_XM32, BUSTER_X86_VEX_THREE) \
    X(DIVSS_X_XM32, VDIVSS_X_X_XM32, BUSTER_X86_VEX_THREE) \
    X(ADDSD_X_XM64, VADDSD_X_X_XM64, BUSTER_X86_VEX_THREE) \
    X(SUBSD_X_XM64, VSUBSD_X_X_XM64, BUSTER_X86_VEX_THREE) \
    X(MULSD_X_XM64, VMULSD_X_X_XM64, BUSTER_X86_VEX_THREE) \
    X(DIVSD_X_XM64, VDIVSD_X_X_XM64, BUSTER_X86_VEX_THREE) \
    X(CMPSS_X_XM32_IMM8, VCMPSS_X_X_XM32_IMM8, BUSTER_X86_VEX_THREE) \
    X(CMPSD_X_XM64_IMM8, VCMPSD_X_X_XM64_IMM8, BUSTER_X86_VEX_THREE) \
    X(CVTSS2SD_X_XM32, VCVTSS2SD_X_X_XM32, BUSTER_X86_VEX_THREE) \
    X(CVTSD2SS_X_XM64, VCVTSD2SS_X_X_XM64, BUSTER_X86_VEX_THREE) \
    X(CVTSI2SS_X_RM32, VCVTSI2SS_X_X_RM32, BUSTER_X86_VEX_THREE) \
    X(CVTSI2SS_X_RM64, VCVTSI2SS_X_X_RM64, BUSTER_X86_VEX_THREE) \
    X(CVTSI2SD_X_RM32, VCVTSI2SD_X_X_RM32, BUSTER_X86_VEX_THREE) \
    X(CVTSI2SD_X_RM64, VCVTSI2SD_X_X_RM64, BUSTER_X86_VEX_THREE) \
    X(XORPS_X_XM128, VXORPS_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PXOR_X_XM128, VPXOR_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PAND_X_XM128, VPAND_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(POR_X_XM128, VPOR_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PADDD_X_XM128, VPADDD_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PADDQ_X_XM128, VPADDQ_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PSUBD_X_XM128, VPSUBD_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PMULLD_X_XM128, VPMULLD_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PCMPEQD_X_XM128, VPCMPEQD_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PUNPCKLBW_X_XM128, VPUNPCKLBW_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(PUNPCKLWD_X_XM128, VPUNPCKLWD_X_X_XM128, BUSTER_X86_VEX_THREE) \
    X(UCOMISS_X_XM32, VUCOMISS_X_XM32, 0) \
    X(UCOMISD_X_XM64, VUCOMISD_X_XM64, 0) \
    X(CVTTSS2SI_R32_XM32, VCVTTSS2SI_R32_XM32, 0) \
    X(CVTTSS2SI_R64_XM32, VCVTTSS2SI_R64_XM32, 0) \
    X(CVTTSD2SI_R32_XM64, VCVTTSD2SI_R32_XM64, 0) \
    X(CVTTSD2SI_R64_XM64, VCVTTSD2SI_R64_XM64, 0) \
    X(CVTDQ2PS_X_XM128, VCVTDQ2PS_X_XM128, 0) \
    X(CVTTPS2DQ_X_XM128, VCVTTPS2DQ_X_XM128, 0) \
    X(MOVD_X_RM32, VMOVD_X_RM32, 0) \
    X(MOVD_RM32_X, VMOVD_RM32_X, 0) \
    X(MOVQ_X_RM64, VMOVQ_X_RM64, 0) \
    X(MOVQ_RM64_X, VMOVQ_RM64_X, 0) \
    X(MOVAPS_X_XM, VMOVAPS_X_XM128, 0) \
    X(MOVAPS_XM_X, VMOVAPS_XM128_X, 0) \
    X(MOVUPS_X_XM, VMOVUPS_X_XM128, 0) \
    X(MOVUPS_XM_X, VMOVUPS_XM128_X, 0) \
    X(PSHUFD_X_XM128_IMM8, VPSHUFD_X_XM128_IMM8, 0) \
    X(MOVSS_X_XM32, VMOVSS_X_M32, 0) \
    X(MOVSS_XM32_X, VMOVSS_M32_X, 0) \
    X(MOVSD_X_XM64, VMOVSD_X_M64, 0) \
    X(MOVSD_XM64_X, VMOVSD_M64_X, 0)

// One entry past the forms, so that a dword gather of the last form's entry stays inside the table.
#define BUSTER_X86_VEX_FORWARD(legacy, twin, three) [X86_FORM_##legacy] = (uint16_t)(X86_FORM_##twin | (three)),
static __attribute__((unused)) const uint16_t x86_vex_forms[X86_FORM_COUNT + 1] = { BUSTER_X86_VEX_TWINS(BUSTER_X86_VEX_FORWARD) };
#undef BUSTER_X86_VEX_FORWARD
// Each twin's legacy form, for the code's certificate (x86_vex_canonical).
#define BUSTER_X86_VEX_BACKWARD(legacy, twin, three) [X86_FORM_##twin] = (uint16_t)(X86_FORM_##legacy | (three)),
// A legacy movss's or movsd's store form is its own entry, which x86_vex_canonical takes between registers.
static __attribute__((unused)) const uint16_t x86_vex_legacy[X86_FORM_COUNT + 1] = {
    BUSTER_X86_VEX_TWINS(BUSTER_X86_VEX_BACKWARD)[X86_FORM_MOVSS_XM32_X] = X86_FORM_MOVSS_XM32_X,
    [X86_FORM_MOVSD_XM64_X] = X86_FORM_MOVSD_XM64_X,
    [X86_FORM_VMOVSS_X_X_X] = X86_FORM_MOVSS_X_XM32,
    [X86_FORM_VMOVSD_X_X_X] = X86_FORM_MOVSD_X_XM64,
};
#undef BUSTER_X86_VEX_BACKWARD

// A legacy SSE record's VEX twin (x86_vex_forms' entry `entry`, a segment override's rows `segment` past the form's
// row). movss and movsd from memory and into it are vmovss's and vmovsd's loads and stores; between registers, their
// merge of the source's low element into the destination's, vmovss's and vmovsd's of three registers with the
// destination's other elements from VEX.vvvv: movss x, y is vmovss x, x, y, and its store form's movss y, x (into y's low
// element) vmovss y, y, x.
static __attribute__((unused)) X86Instruction x86_vex_record(X86Instruction record, uint32_t entry, uint32_t segment)
{
    const uint32_t legacy = record.form & BUSTER_X86_FORM_NUMBER;
    const uint32_t row = legacy - segment;
    const bool direct = record.mode == BUSTER_X86_REGISTER_DIRECT;
    const bool single = row == X86_FORM_MOVSS_X_XM32 || row == X86_FORM_MOVSS_XM32_X;
    const bool merge = direct && (single || row == X86_FORM_MOVSD_X_XM64 || row == X86_FORM_MOVSD_XM64_X);
    const bool into_rm = row == X86_FORM_MOVSS_XM32_X || row == X86_FORM_MOVSD_XM64_X;
    X86Instruction twin = record;
    if (merge)
    {
        const uint8_t destination = into_rm ? record.rm : record.reg;
        const uint8_t source = into_rm ? record.reg : record.rm;
        twin.form = (uint16_t)((single ? X86_FORM_VMOVSS_X_X_X : X86_FORM_VMOVSD_X_X_X) + segment);
        twin.reg = destination;
        twin.vvvv = destination;
        twin.rm = source;
    }
    else
    {
        twin.form = (uint16_t)((record.form & BUSTER_X86_FORM_FIXUP) | ((entry & BUSTER_X86_VEX_FORM) + segment));
        twin.vvvv = (entry & BUSTER_X86_VEX_THREE) != 0 ? record.reg : 0;
    }
    return twin;
}

// The records of `records[0, count)`, at most 16, whose forms have an entry in `table` (x86_vex_forms or
// x86_vex_legacy, one entry past the forms): their forms gathered, each one's entry gathered by its row (its number
// less its segment override's rows, FS's and GS's, which every row has; an SS override, past them, has none).
static __attribute__((unused)) __mmask16 x86_vex_lookup(const X86Instruction* records, uint32_t count, const uint16_t* table)
{
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i rows = _mm512_set1_epi32(X86_FORM_COUNT);
    const __mmask16 live = count >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << count) - 1);
    const __m512i forms = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), live, _mm512_slli_epi32(lanes, 4), (const uint8_t*)records + offsetof(X86Instruction, form), 1);
    const __m512i number = _mm512_and_si512(forms, _mm512_set1_epi32(BUSTER_X86_FORM_NUMBER));
    const __mmask16 plain = _mm512_cmplt_epu32_mask(number, _mm512_set1_epi32(BUSTER_X86_FORM_SS)) & live;
    __m512i row = _mm512_mask_sub_epi32(number, _mm512_cmpge_epu32_mask(number, rows), number, rows);
    row = _mm512_mask_sub_epi32(row, _mm512_cmpge_epu32_mask(row, rows), row, rows);
    const __m512i entries = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), plain, row, table, 2), _mm512_set1_epi32(0xffff));
    return _mm512_test_epi32_mask(entries, entries) & plain;
}

// A record's canonical form for Tier 1's code certificate (#2830), which decodes and compares Tier 1's legacy SSE records:
// a VEX twin (x86_vex_record's, a three-operand one's VEX.vvvv its destination) as its legacy record, and a
// legacy movss or movsd into its ModRM.rm register as the same merge into its ModRM.reg one. Two records of one canonical
// form compute the same low 128 bits; the twin also zeroes the bits above them, which the certificate's register cells
// (a location is a register's low bytes, its whole width or a scalar's) never read apart from those it writes.
static __attribute__((unused)) X86Instruction x86_vex_canonical(X86Instruction record)
{
    const uint32_t number = record.form & BUSTER_X86_FORM_NUMBER;
    const uint32_t segment = number < BUSTER_X86_FORM_SS ? number - number % X86_FORM_COUNT : 0;
    const uint32_t row = number < BUSTER_X86_FORM_SS ? number - segment : 0;
    const uint32_t entry = x86_vex_legacy[row];
    const bool direct = record.mode == BUSTER_X86_REGISTER_DIRECT;
    X86Instruction canonical = record;
    const bool merge = row == X86_FORM_VMOVSS_X_X_X || row == X86_FORM_VMOVSD_X_X_X;
    if (direct && (row == X86_FORM_MOVSS_XM32_X || row == X86_FORM_MOVSD_XM64_X))
    {
        canonical.form = (uint16_t)((row == X86_FORM_MOVSS_XM32_X ? X86_FORM_MOVSS_X_XM32 : X86_FORM_MOVSD_X_XM64) + segment);
        canonical.reg = record.rm;
        canonical.rm = record.reg;
    }
    else if (entry != 0 && ((entry & BUSTER_X86_VEX_THREE) == 0 || record.vvvv == record.reg) && (!merge || record.vvvv == record.reg))
    {
        canonical.form = (uint16_t)((record.form & BUSTER_X86_FORM_FIXUP) | ((entry & BUSTER_X86_VEX_FORM) + segment));
        canonical.vvvv = 0;
    }
    return canonical;
}

// Tier 0's twins past Tier 1's (#3335), for the forms its copies write and Tier 1's selection doesn't: an 8-byte move's
// movq into an xmm register and out of one, and the upper half's movhps of a 16-byte copy in halves (tier0_copy). Tier 1's
// certificate never reads them (x86_vex_legacy has none of them), so that they are Tier 0's table's alone.
#define BUSTER_X86_VEX_TWINS_TIER0(X) \
    X(MOVQ_X_XM64, VMOVQ_X_XM64, 0) \
    X(MOVQ_XM64_X, VMOVQ_XM64_X, 0) \
    X(MOVHPS_X_M64, VMOVHPS_X_X_M64, BUSTER_X86_VEX_THREE)

// Tier 0's table (tier0_vex): Tier 1's pairs and its own, one entry past the forms, as x86_vex_forms is.
#define BUSTER_X86_VEX_FORWARD(legacy, twin, three) [X86_FORM_##legacy] = (uint16_t)(X86_FORM_##twin | (three)),
static __attribute__((unused)) const uint16_t x86_vex_forms_tier0[X86_FORM_COUNT + 1] = { BUSTER_X86_VEX_TWINS(BUSTER_X86_VEX_FORWARD) BUSTER_X86_VEX_TWINS_TIER0(BUSTER_X86_VEX_FORWARD) };
#undef BUSTER_X86_VEX_FORWARD

// Whether `records` name ymm0 to ymm15 or zmm0 to zmm15 (#2830): the upper state they leave dirty, which a legacy SSE
// instruction after them (a caller's, glibc's, or another compiler's) then runs in, waiting for its destination's old
// value. As clang's VZeroUpperInsertion counts it, a register operand of a form's ymm or zmm class (VEX.vvvv's, ModRM.reg's,
// or ModRM.rm's where the record's mode is a register), read or written, below 16: vzeroupper clears theirs, and no legacy
// instruction reaches zmm16 to zmm31. Sixteen records a step: their registers' and modes' dwords and their forms' gathered,
// each form's row (its number less its segment override's rows, BUSTER_X86_FORM_FS's multiples) and its row's three
// operands' classes gathered from the form table.
static __attribute__((unused)) bool x86_upper_dirty(const X86Instruction* records, uint32_t count)
{
    _Static_assert(offsetof(X86FormDescription, rm) == offsetof(X86FormDescription, reg) + 2, "a dword gather reads a form's reg and rm operands");
    _Static_assert(offsetof(X86Instruction, rm) == offsetof(X86Instruction, reg) + 1 && offsetof(X86Instruction, mode) == offsetof(X86Instruction, reg) + 3, "a record's reg, rm and mode bytes in one dword");
    _Static_assert(offsetof(X86Instruction, vvvv) == offsetof(X86Instruction, form) + 2, "a record's form and vvvv in one dword");
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i strides = _mm512_slli_epi32(lanes, 4);
    const __m512i rows = _mm512_set1_epi32(X86_FORM_COUNT);
    const __m512i bytes = _mm512_set1_epi32(0xff);
    const __m512i sixteen = _mm512_set1_epi32(16);
    const __m512i classes = _mm512_set1_epi32(BUSTER_X86_OPERAND_CLASS & ~1);
    const __m512i wide = _mm512_set1_epi32(BUSTER_X86_CLASS_YMM);
    const __m512i registered = _mm512_set1_epi32(BUSTER_X86_OPERAND_REGISTER);
    const __m512i size = _mm512_set1_epi32((int32_t)sizeof(X86FormDescription));
    const uint8_t* table = (const uint8_t*)x86_form_descriptions;
    __mmask16 dirty = 0;
    for (uint32_t first = 0; first < count && dirty == 0; first += 16)
    {
        const uint32_t left = count - first;
        const __mmask16 live = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        const uint8_t* base = (const uint8_t*)(records + first);
        const __m512i operands = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), live, strides, base + offsetof(X86Instruction, reg), 1);
        const __m512i forms = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), live, strides, base + offsetof(X86Instruction, form), 1);
        __m512i row = _mm512_and_si512(forms, _mm512_set1_epi32(BUSTER_X86_FORM_NUMBER));
        for (uint32_t segment = 0; segment < 3; segment += 1)
        {
            row = _mm512_mask_sub_epi32(row, _mm512_cmpge_epu32_mask(row, rows), row, rows);
        }
        const __m512i offsets = _mm512_mullo_epi32(row, size);
        const __m512i reg_rm = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), live, offsets, table + offsetof(X86FormDescription, reg), 1);
        const __m512i vvvv = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), live, offsets, table + offsetof(X86FormDescription, vvvv), 1);
        const __m512i reg_operand = reg_rm;
        const __m512i rm_operand = _mm512_srli_epi32(reg_rm, 16);
        // Each operand of a ymm or zmm class (6 or 7) at a register below 16.
        const __mmask16 reg_hit = _mm512_cmpeq_epi32_mask(_mm512_and_si512(reg_operand, classes), wide) &
                                  _mm512_cmplt_epu32_mask(_mm512_and_si512(operands, bytes), sixteen);
        const __mmask16 rm_hit = _mm512_cmpeq_epi32_mask(_mm512_and_si512(rm_operand, classes), wide) & _mm512_test_epi32_mask(rm_operand, registered) &
                                 _mm512_cmplt_epu32_mask(_mm512_and_si512(_mm512_srli_epi32(operands, 8), bytes), sixteen) &
                                 _mm512_cmpeq_epi32_mask(_mm512_srli_epi32(operands, 24), _mm512_set1_epi32(BUSTER_X86_REGISTER_DIRECT));
        const __mmask16 vvvv_hit = _mm512_cmpeq_epi32_mask(_mm512_and_si512(vvvv, classes), wide) & _mm512_test_epi32_mask(vvvv, registered) &
                                   _mm512_cmplt_epu32_mask(_mm512_and_si512(_mm512_srli_epi32(forms, 16), bytes), sixteen);
        dirty = (__mmask16)((reg_hit | rm_hit | vvvv_hit) & live);
    }
    return dirty != 0;
}

#endif
