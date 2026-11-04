// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Reviewed, unmasked equivalents for the pre-AVX-512 builtin rows. This is not a prefix rewrite: opmask compares,
// broadcast, suppression of exceptions, rounding and implicit-register forms are deliberately not translated.
// Legacy destructive destinations are tied explicitly, with a copy only when it cannot overwrite another source.

static uint32_t check_vector_baseline_form(uint32_t form, bool avx)
{
    uint32_t result = form;
    switch (form)
    {
#define BUSTER_BASE_FORM(evex, vex, legacy) case X86_FORM_##evex: result = avx ? X86_FORM_##vex : X86_FORM_##legacy; break;
#define BUSTER_BASE_BINARY(name) BUSTER_BASE_FORM(V##name##_X_X_XM128_EVEX, V##name##_X_X_XM128, name##_X_XM128) \
    BUSTER_BASE_FORM(V##name##_Y_Y_YM256_EVEX, V##name##_Y_Y_YM256, NONE)
        BUSTER_BASE_BINARY(PACKSSWB)
        BUSTER_BASE_BINARY(PACKSSDW)
        BUSTER_BASE_BINARY(PACKUSWB)
        BUSTER_BASE_BINARY(PMADDWD)
        BUSTER_BASE_BINARY(PMULHW)
        BUSTER_BASE_BINARY(PMULHUW)
        BUSTER_BASE_BINARY(PMULUDQ)
        BUSTER_BASE_BINARY(PMULDQ)
        BUSTER_BASE_BINARY(MAXPS)
        BUSTER_BASE_BINARY(MINPS)
        BUSTER_BASE_BINARY(MAXPD)
        BUSTER_BASE_BINARY(MINPD)
        BUSTER_BASE_FORM(VSHUFPS_X_X_XM128_IMM8_EVEX, VSHUFPS_X_X_XM128_IMM8, SHUFPS_X_XM128_IMM8)
        BUSTER_BASE_FORM(VSHUFPD_X_X_XM128_IMM8, VSHUFPD_X_X_XM128_IMM8, SHUFPD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VSHUFPD_X_X_XM128_IMM8_EVEX, VSHUFPD_X_X_XM128_IMM8, SHUFPD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFD_X_XM128_IMM8_EVEX, VPSHUFD_X_XM128_IMM8, PSHUFD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFHW_X_XM128_IMM8_EVEX, VPSHUFHW_X_XM128_IMM8, PSHUFHW_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFLW_X_XM128_IMM8_EVEX, VPSHUFLW_X_XM128_IMM8, PSHUFLW_X_XM128_IMM8)
        BUSTER_BASE_FORM(VCVTPS2DQ_X_XM128_EVEX, VCVTPS2DQ_X_XM128, CVTPS2DQ_X_XM128)
        BUSTER_BASE_FORM(VCVTTPS2DQ_X_XM128_EVEX, VCVTTPS2DQ_X_XM128, CVTTPS2DQ_X_XM128)
        BUSTER_BASE_FORM(VCVTDQ2PS_X_XM128_EVEX, VCVTDQ2PS_X_XM128, CVTDQ2PS_X_XM128)
        BUSTER_BASE_FORM(VMOVD_X_RM32_EVEX, VMOVD_X_RM32, MOVD_X_RM32)
        BUSTER_BASE_FORM(VMOVD_RM32_X_EVEX, VMOVD_RM32_X, MOVD_RM32_X)
        BUSTER_BASE_FORM(VMOVQ_RM64_X_EVEX, VMOVQ_RM64_X, MOVQ_RM64_X)
        BUSTER_BASE_FORM(VMOVQ_X_XM64_EVEX, VMOVQ_X_XM64, MOVQ_X_XM64)
        BUSTER_BASE_FORM(VMOVQ_X_RM64_EVEX, VMOVQ_X_RM64, MOVQ_X_RM64)
        BUSTER_BASE_FORM(VPEXTRW_R32_X_IMM8_EVEX, VPEXTRW_R32_X_IMM8, PEXTRW_R32_X_IMM8)
        BUSTER_BASE_FORM(VPINSRW_X_X_RM32_IMM8_EVEX, VPINSRW_X_X_RM32_IMM8, PINSRW_X_RM32_IMM8)
        // SSE2's averages and sums of differences (#775): AVX-512BW's EVEX forms, VEX's or legacy SSE's at the baseline.
        BUSTER_BASE_BINARY(PAVGB)
        BUSTER_BASE_BINARY(PAVGW)
        BUSTER_BASE_BINARY(PSADBW)
        // The elementwise builtins' minimum, maximum and saturating arithmetic: SSE2's of unsigned bytes and signed words,
        // SSE4.1's of the others (x86_target_requirement refuses those at the baseline), and SSSE3's absolute values.
        BUSTER_BASE_BINARY(PMAXUB)
        BUSTER_BASE_BINARY(PMINUB)
        BUSTER_BASE_BINARY(PMAXSW)
        BUSTER_BASE_BINARY(PMINSW)
        BUSTER_BASE_BINARY(PMAXSB)
        BUSTER_BASE_BINARY(PMINSB)
        BUSTER_BASE_BINARY(PMAXUW)
        BUSTER_BASE_BINARY(PMINUW)
        BUSTER_BASE_BINARY(PMAXSD)
        BUSTER_BASE_BINARY(PMINSD)
        BUSTER_BASE_BINARY(PMAXUD)
        BUSTER_BASE_BINARY(PMINUD)
        BUSTER_BASE_BINARY(PADDSB)
        BUSTER_BASE_BINARY(PADDSW)
        BUSTER_BASE_BINARY(PADDUSB)
        BUSTER_BASE_BINARY(PADDUSW)
        BUSTER_BASE_BINARY(PSUBSB)
        BUSTER_BASE_BINARY(PSUBSW)
        BUSTER_BASE_BINARY(PSUBUSB)
        BUSTER_BASE_BINARY(PSUBUSW)
        // SSE3's, SSSE3's and SSE4.1's (x86-64-v2's) binary operations, AVX's VEX forms of them at x86-64-v3.
        BUSTER_BASE_BINARY(PSHUFB)
        BUSTER_BASE_BINARY(PMADDUBSW)
        BUSTER_BASE_BINARY(PACKUSDW)
        BUSTER_BASE_BINARY(PMULHRSW)
#define BUSTER_BASE_VEX(name) BUSTER_BASE_FORM(V##name##_X_X_XM128, V##name##_X_X_XM128, name##_X_XM128)
        BUSTER_BASE_VEX(ADDSUBPS)
        BUSTER_BASE_VEX(ADDSUBPD)
        BUSTER_BASE_VEX(HADDPS)
        BUSTER_BASE_VEX(HADDPD)
        BUSTER_BASE_VEX(HSUBPS)
        BUSTER_BASE_VEX(HSUBPD)
        BUSTER_BASE_VEX(PHADDW)
        BUSTER_BASE_VEX(PHADDD)
        BUSTER_BASE_VEX(PHADDSW)
        BUSTER_BASE_VEX(PHSUBW)
        BUSTER_BASE_VEX(PHSUBD)
        BUSTER_BASE_VEX(PHSUBSW)
        BUSTER_BASE_VEX(PSIGNB)
        BUSTER_BASE_VEX(PSIGNW)
        BUSTER_BASE_VEX(PSIGND)
#undef BUSTER_BASE_VEX
        BUSTER_BASE_FORM(VPALIGNR_X_X_XM128_IMM8_EVEX, VPALIGNR_X_X_XM128_IMM8, PALIGNR_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPHMINPOSUW_X_XM128, VPHMINPOSUW_X_XM128, PHMINPOSUW_X_XM128)
        BUSTER_BASE_FORM(VPTEST_X_XM128, VPTEST_X_XM128, PTEST_X_XM128)
        // LDDQU's unaligned load, which legacy SSE3's encoder form isn't one of: MOVUPS's, the same bytes.
        BUSTER_BASE_FORM(VLDDQU_X_M128, VLDDQU_X_M128, MOVUPS_X_XM)
        // SSE4.1's sign and zero extensions, the integer conversions' widening rows.
#define BUSTER_BASE_EXTEND(name, x, y) BUSTER_BASE_FORM(V##name##_X_##x##_EVEX, V##name##_X_##x, name##_X_##x) \
        BUSTER_BASE_FORM(V##name##_Y_##y##_EVEX, V##name##_Y_##y, NONE)
        BUSTER_BASE_EXTEND(PMOVSXBW, XM64, XM128)
        BUSTER_BASE_EXTEND(PMOVSXBD, XM32, XM64)
        BUSTER_BASE_EXTEND(PMOVSXBQ, XM16, XM32)
        BUSTER_BASE_EXTEND(PMOVSXWD, XM64, XM128)
        BUSTER_BASE_EXTEND(PMOVSXWQ, XM32, XM64)
        BUSTER_BASE_EXTEND(PMOVSXDQ, XM64, XM128)
        BUSTER_BASE_EXTEND(PMOVZXBW, XM64, XM128)
        BUSTER_BASE_EXTEND(PMOVZXBD, XM32, XM64)
        BUSTER_BASE_EXTEND(PMOVZXBQ, XM16, XM32)
        BUSTER_BASE_EXTEND(PMOVZXWD, XM64, XM128)
        BUSTER_BASE_EXTEND(PMOVZXWQ, XM32, XM64)
        BUSTER_BASE_EXTEND(PMOVZXDQ, XM64, XM128)
#undef BUSTER_BASE_EXTEND
        // AVX's and AVX2's forms of one width (x86-64-v3's): variable shifts and permutes, F16C's conversion and FMA's.
#define BUSTER_BASE_AVX(name, x, y) BUSTER_BASE_FORM(V##name##_X_##x##_EVEX, V##name##_X_##x, NONE) \
        BUSTER_BASE_FORM(V##name##_Y_##y##_EVEX, V##name##_Y_##y, NONE)
        BUSTER_BASE_AVX(PSLLVD, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PSLLVQ, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PSRLVD, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PSRLVQ, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PSRAVD, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PERMILPS, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(PERMILPD, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(CVTPH2PS, XM64, XM128)
        BUSTER_BASE_AVX(FMADD213PS, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(FMADD213PD, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(FMADDSUB213PS, X_XM128, Y_YM256)
        BUSTER_BASE_AVX(FMADDSUB213PD, X_XM128, Y_YM256)
#undef BUSTER_BASE_AVX
        // SSE4.1's blends, dot products, roundings, sums of differences and elements' extractions and insertions; AVX's
        // VEX forms of them, and of the 256-bit lanes' and immediate permutes'.
#define BUSTER_BASE_IMMEDIATE(vex, legacy) BUSTER_BASE_FORM(vex, vex, legacy)
        BUSTER_BASE_IMMEDIATE(VPBLENDW_X_X_XM128_IMM8, PBLENDW_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VBLENDPS_X_X_XM128_IMM8, BLENDPS_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VBLENDPD_X_X_XM128_IMM8, BLENDPD_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VDPPS_X_X_XM128_IMM8, DPPS_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VDPPD_X_X_XM128_IMM8, DPPD_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VROUNDPS_X_XM128_IMM8, ROUNDPS_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VROUNDPD_X_XM128_IMM8, ROUNDPD_X_XM128_IMM8)
        BUSTER_BASE_IMMEDIATE(VROUNDSS_X_X_XM32_IMM8, ROUNDSS_X_XM32_IMM8)
        BUSTER_BASE_IMMEDIATE(VROUNDSD_X_X_XM64_IMM8, ROUNDSD_X_XM64_IMM8)
        BUSTER_BASE_IMMEDIATE(VMPSADBW_X_X_XM128_IMM8, MPSADBW_X_XM128_IMM8)
#undef BUSTER_BASE_IMMEDIATE
        BUSTER_BASE_FORM(VPEXTRB_RM32_X_IMM8_EVEX, VPEXTRB_RM32_X_IMM8, PEXTRB_RM32_X_IMM8)
        BUSTER_BASE_FORM(VPEXTRD_RM32_X_IMM8_EVEX, VPEXTRD_RM32_X_IMM8, PEXTRD_RM32_X_IMM8)
        BUSTER_BASE_FORM(VPEXTRQ_RM64_X_IMM8_EVEX, VPEXTRQ_RM64_X_IMM8, PEXTRQ_RM64_X_IMM8)
        BUSTER_BASE_FORM(VPINSRB_X_X_RM32_IMM8_EVEX, VPINSRB_X_X_RM32_IMM8, PINSRB_X_RM32_IMM8)
        BUSTER_BASE_FORM(VPINSRD_X_X_RM32_IMM8_EVEX, VPINSRD_X_X_RM32_IMM8, PINSRD_X_RM32_IMM8)
        BUSTER_BASE_FORM(VPINSRQ_X_X_RM64_IMM8_EVEX, VPINSRQ_X_X_RM64_IMM8, PINSRQ_X_RM64_IMM8)
        // VPERMILPS's immediate permutes of 16 bytes are PSHUFD's (a float's extraction, vec_ext_v4sf's).
        BUSTER_BASE_FORM(VPERMILPS_X_XM128_IMM8_EVEX, VPERMILPS_X_XM128_IMM8, PSHUFD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPERMILPS_Y_YM256_IMM8_EVEX, VPERMILPS_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPERMILPD_X_XM128_IMM8_EVEX, VPERMILPD_X_XM128_IMM8, NONE)
        BUSTER_BASE_FORM(VPERMILPD_Y_YM256_IMM8_EVEX, VPERMILPD_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPERMPD_Y_YM256_IMM8_EVEX, VPERMPD_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPERMQ_Y_YM256_IMM8_EVEX, VPERMQ_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VSHUFPS_Y_Y_YM256_IMM8_EVEX, VSHUFPS_Y_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VSHUFPD_Y_Y_YM256_IMM8_EVEX, VSHUFPD_Y_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPSHUFD_Y_YM256_IMM8_EVEX, VPSHUFD_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPSHUFHW_Y_YM256_IMM8_EVEX, VPSHUFHW_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPSHUFLW_Y_YM256_IMM8_EVEX, VPSHUFLW_Y_YM256_IMM8, NONE)
        BUSTER_BASE_FORM(VPSLLDQ_Y_YM256_IMM8, VPSLLDQ_Y_Y_IMM8, NONE)
        BUSTER_BASE_FORM(VPSRLDQ_Y_YM256_IMM8, VPSRLDQ_Y_Y_IMM8, NONE)
        // A 256-bit vector's 16-byte lanes, AVX's VEXTRACTF128 and VINSERTF128 (whose bits are an integer's too).
        BUSTER_BASE_FORM(VEXTRACTF32X4_XM128_Y_IMM8, VEXTRACTF128_XM128_Y_IMM8, NONE)
        BUSTER_BASE_FORM(VEXTRACTI32X4_XM128_Y_IMM8, VEXTRACTF128_XM128_Y_IMM8, NONE)
        BUSTER_BASE_FORM(VINSERTF32X4_Y_Y_XM128_IMM8, VINSERTF128_Y_Y_XM128_IMM8, NONE)
        BUSTER_BASE_FORM(VINSERTI32X4_Y_Y_XM128_IMM8, VINSERTF128_Y_Y_XM128_IMM8, NONE)
        BUSTER_BASE_FORM(VPERMD_Y_Y_YM256_EVEX, VPERMD_Y_Y_YM256, NONE)
        BUSTER_BASE_FORM(VPERMPS_Y_Y_YM256_EVEX, VPERMPS_Y_Y_YM256, NONE)
        // Whole-register moves without a mask (an unmasked merge's, a load's or store's), and an undefined vector's
        // zeros: MOVUPS's and XORPS's.
#define BUSTER_BASE_MOVE(name) BUSTER_BASE_FORM(V##name##_X_XM128, VMOVUPS_X_XM128, MOVUPS_X_XM) \
        BUSTER_BASE_FORM(V##name##_Y_YM256, VMOVUPS_Y_YM256, NONE) \
        BUSTER_BASE_FORM(V##name##_XM128_X, VMOVUPS_XM128_X, MOVUPS_XM_X) \
        BUSTER_BASE_FORM(V##name##_YM256_Y, VMOVUPS_YM256_Y, NONE)
        BUSTER_BASE_MOVE(MOVDQU8)
        BUSTER_BASE_MOVE(MOVDQU16)
        BUSTER_BASE_MOVE(MOVDQU32)
        BUSTER_BASE_MOVE(MOVDQU64)
#undef BUSTER_BASE_MOVE
        BUSTER_BASE_FORM(VPXORQ_X_X_XM128, VXORPS_X_X_XM128, XORPS_X_XM128)
        BUSTER_BASE_FORM(VPXORQ_Y_Y_YM256, VXORPS_Y_Y_YM256, NONE)
        // The non-temporal stores and load.
        BUSTER_BASE_FORM(VMOVNTPS_M128_X_EVEX, VMOVNTPS_M128_X, MOVNTPS_M128_X)
        BUSTER_BASE_FORM(VMOVNTPD_M128_X_EVEX, VMOVNTPD_M128_X, MOVNTPD_M128_X)
        BUSTER_BASE_FORM(VMOVNTDQ_M128_X_EVEX, VMOVNTDQ_M128_X, MOVNTDQ_M128_X)
        BUSTER_BASE_FORM(VMOVNTPS_M256_Y_EVEX, VMOVNTPS_M256_Y, NONE)
        BUSTER_BASE_FORM(VMOVNTPD_M256_Y_EVEX, VMOVNTPD_M256_Y, NONE)
        BUSTER_BASE_FORM(VMOVNTDQ_M256_Y_EVEX, VMOVNTDQ_M256_Y, NONE)
        BUSTER_BASE_FORM(VMOVNTDQA_X_M128_EVEX, VMOVNTDQA_X_M128, MOVNTDQA_X_M128)
        BUSTER_BASE_FORM(VMOVNTDQA_Y_M256_EVEX, VMOVNTDQA_Y_M256, NONE)
        BUSTER_BASE_FORM(VPABSB_X_XM128_EVEX, VPABSB_X_XM128, PABSB_X_XM128)
        BUSTER_BASE_FORM(VPABSW_X_XM128_EVEX, VPABSW_X_XM128, PABSW_X_XM128)
        BUSTER_BASE_FORM(VPABSD_X_XM128_EVEX, VPABSD_X_XM128, PABSD_X_XM128)
        BUSTER_BASE_FORM(VPABSB_Y_YM256_EVEX, VPABSB_Y_YM256, NONE)
        BUSTER_BASE_FORM(VPABSW_Y_YM256_EVEX, VPABSW_Y_YM256, NONE)
        BUSTER_BASE_FORM(VPABSD_Y_YM256_EVEX, VPABSD_Y_YM256, NONE)
        // The scalar operations of a vector's low element, the others the first source's (a legacy form's destination).
#define BUSTER_BASE_SCALAR(name, memory) BUSTER_BASE_FORM(V##name##_X_X_##memory##_EVEX, V##name##_X_X_##memory, name##_X_##memory)
        BUSTER_BASE_SCALAR(MAXSS, XM32)
        BUSTER_BASE_SCALAR(MINSS, XM32)
        BUSTER_BASE_SCALAR(MAXSD, XM64)
        BUSTER_BASE_SCALAR(MINSD, XM64)
        BUSTER_BASE_SCALAR(SQRTSS, XM32)
        BUSTER_BASE_SCALAR(SQRTSD, XM64)
        BUSTER_BASE_SCALAR(CVTSD2SS, XM64)
        BUSTER_BASE_SCALAR(CVTSS2SD, XM32)
#undef BUSTER_BASE_SCALAR
        BUSTER_BASE_FORM(VRCPSS_X_X_XM32, VRCPSS_X_X_XM32, RCPSS_X_XM32)
        BUSTER_BASE_FORM(VRSQRTSS_X_X_XM32, VRSQRTSS_X_X_XM32, RSQRTSS_X_XM32)
        // SSE's compares into a vector, by predicates 0 to 7 alone without AVX (check_vector_baseline_row), and into the
        // flags (comiss's and the others', which name no vvvv).
        BUSTER_BASE_FORM(VCMPPS_X_X_XM128_IMM8, VCMPPS_X_X_XM128_IMM8, CMPPS_X_XM128_IMM8)
        BUSTER_BASE_FORM(VCMPPD_X_X_XM128_IMM8, VCMPPD_X_X_XM128_IMM8, CMPPD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VCMPSS_X_X_XM32_IMM8, VCMPSS_X_X_XM32_IMM8, CMPSS_X_XM32_IMM8)
        BUSTER_BASE_FORM(VCMPSD_X_X_XM64_IMM8, VCMPSD_X_X_XM64_IMM8, CMPSD_X_XM64_IMM8)
        BUSTER_BASE_FORM(VCOMISS_X_XM32_EVEX, VCOMISS_X_XM32, COMISS_X_XM32)
        BUSTER_BASE_FORM(VCOMISD_X_XM64_EVEX, VCOMISD_X_XM64, COMISD_X_XM64)
        BUSTER_BASE_FORM(VUCOMISS_X_XM32_EVEX, VUCOMISS_X_XM32, UCOMISS_X_XM32)
        BUSTER_BASE_FORM(VUCOMISD_X_XM64_EVEX, VUCOMISD_X_XM64, UCOMISD_X_XM64)
        // The packed conversions and approximations of one source, and the square roots.
#define BUSTER_BASE_UNARY(name, x, y) BUSTER_BASE_FORM(V##name##_X_##x##_EVEX, V##name##_X_##x, name##_X_##x) \
        BUSTER_BASE_FORM(V##name##_##y##_EVEX, V##name##_##y, NONE)
        BUSTER_BASE_UNARY(CVTPD2DQ, XM128, X_YM256)
        BUSTER_BASE_UNARY(CVTTPD2DQ, XM128, X_YM256)
        BUSTER_BASE_UNARY(CVTPD2PS, XM128, X_YM256)
        BUSTER_BASE_UNARY(CVTPS2PD, XM64, Y_XM128)
        BUSTER_BASE_UNARY(CVTDQ2PD, XM64, Y_XM128)
        BUSTER_BASE_UNARY(SQRTPS, XM128, Y_YM256)
        BUSTER_BASE_UNARY(SQRTPD, XM128, Y_YM256)
#undef BUSTER_BASE_UNARY
        BUSTER_BASE_FORM(VCVTPS2DQ_Y_YM256_EVEX, VCVTPS2DQ_Y_YM256, NONE)
        BUSTER_BASE_FORM(VCVTTPS2DQ_Y_YM256_EVEX, VCVTTPS2DQ_Y_YM256, NONE)
        BUSTER_BASE_FORM(VCVTDQ2PS_Y_YM256_EVEX, VCVTDQ2PS_Y_YM256, NONE)
        BUSTER_BASE_FORM(VRCPPS_X_XM128, VRCPPS_X_XM128, RCPPS_X_XM128)
        BUSTER_BASE_FORM(VRSQRTPS_X_XM128, VRSQRTPS_X_XM128, RSQRTPS_X_XM128)
        // #1193: the sign masks and the scalar conversions to integers, which write a general register alone, so there
        // is no destination to tie; the conversions' rows without a rounding control, which no decoration names.
        BUSTER_BASE_FORM(VPMOVMSKB_R32_X, VPMOVMSKB_R32_X, PMOVMSKB_R32_X)
        BUSTER_BASE_FORM(VMOVMSKPS_R32_X, VMOVMSKPS_R32_X, MOVMSKPS_R32_X)
        BUSTER_BASE_FORM(VMOVMSKPD_R32_X, VMOVMSKPD_R32_X, MOVMSKPD_R32_X)
        BUSTER_BASE_FORM(VCVTTSS2SI_R32_XM32_EVEX, VCVTTSS2SI_R32_XM32, CVTTSS2SI_R32_XM32)
        BUSTER_BASE_FORM(VCVTTSS2SI_R64_XM32_EVEX, VCVTTSS2SI_R64_XM32, CVTTSS2SI_R64_XM32)
        BUSTER_BASE_FORM(VCVTTSD2SI_R32_XM64_EVEX, VCVTTSD2SI_R32_XM64, CVTTSD2SI_R32_XM64)
        BUSTER_BASE_FORM(VCVTTSD2SI_R64_XM64_EVEX, VCVTTSD2SI_R64_XM64, CVTTSD2SI_R64_XM64)
        BUSTER_BASE_FORM(VCVTSS2SI_R32_XM32_EVEX, VCVTSS2SI_R32_XM32, CVTSS2SI_R32_XM32)
        BUSTER_BASE_FORM(VCVTSS2SI_R64_XM32_EVEX, VCVTSS2SI_R64_XM32, CVTSS2SI_R64_XM32)
        BUSTER_BASE_FORM(VCVTSD2SI_R32_XM64_EVEX, VCVTSD2SI_R32_XM64, CVTSD2SI_R32_XM64)
        BUSTER_BASE_FORM(VCVTSD2SI_R64_XM64_EVEX, VCVTSD2SI_R64_XM64, CVTSD2SI_R64_XM64)
#define BUSTER_BASE_SHIFT(name) BUSTER_BASE_FORM(V##name##_X_X_XM128_EVEX, V##name##_X_X_XM128, name##_X_XM128) \
        BUSTER_BASE_FORM(V##name##_X_XM128_IMM8, V##name##_X_X_IMM8, name##_X_IMM8) \
        BUSTER_BASE_FORM(V##name##_Y_Y_XM128_EVEX, V##name##_Y_Y_XM128, NONE) \
        BUSTER_BASE_FORM(V##name##_Y_YM256_IMM8, V##name##_Y_Y_IMM8, NONE)
        BUSTER_BASE_SHIFT(PSLLW)
        BUSTER_BASE_SHIFT(PSLLD)
        BUSTER_BASE_SHIFT(PSLLQ)
        BUSTER_BASE_SHIFT(PSRLW)
        BUSTER_BASE_SHIFT(PSRLD)
        BUSTER_BASE_SHIFT(PSRLQ)
        BUSTER_BASE_SHIFT(PSRAW)
        BUSTER_BASE_SHIFT(PSRAD)
        BUSTER_BASE_FORM(VPSLLDQ_X_XM128_IMM8, VPSLLDQ_X_X_IMM8, PSLLDQ_X_IMM8)
        BUSTER_BASE_FORM(VPSRLDQ_X_XM128_IMM8, VPSRLDQ_X_X_IMM8, PSRLDQ_X_IMM8)
#undef BUSTER_BASE_SHIFT
#undef BUSTER_BASE_BINARY
#undef BUSTER_BASE_FORM
        default: break;
    }
    return result;
}

static bool check_vector_baseline_row(const X86Target* target, const Tier0Operation* input, Tier0Operation* output)
{
    *output = *input;
    output->record_count = 0;
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const uint32_t bytes = avx ? 32 : 16;
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    bool valid = input->result_class != BUSTER_TYPED_CLASS_VECTOR || input->result_bytes <= bytes;
    for (uint32_t j = 0; j < input->operand_count; j += 1)
    {
        valid = valid && (input->operand_classes[j] != BUSTER_TYPED_CLASS_VECTOR || input->operand_bytes[j] <= bytes);
    }
    for (uint32_t i = 0; valid && i < input->record_count; i += 1)
    {
        X86Instruction record = input->records[i];
        const uint32_t form = check_vector_baseline_form(record.form, avx);
        const uint64_t required = x86_target_requirement(form);
        valid = form != X86_FORM_NONE && required != 0 && (required & ~available) == 0;
        if (valid && form != record.form)
        {
            const X86FormDescription* before = x86_form_description(record.form);
            const X86FormDescription* after = x86_form_description(form);
            valid = record.decorations == 0;
            if (valid && after->family == BUSTER_X86_FAMILY_LEGACY && before->vvvv != BUSTER_X86_OPERAND_NONE)
            {
                // Immediate packed shifts encode their destination in vvvv, unlike the other binary forms.
                const bool shift = before->reg == BUSTER_X86_OPERAND_NONE;
                const uint8_t to = shift ? record.vvvv : record.reg;
                const uint8_t from = shift ? record.rm : record.vvvv;
                if (to != from)
                {
                    valid = record.mode == BUSTER_X86_REGISTER_DIRECT && (shift || to != record.rm) &&
                            output->record_count + 1 < BUSTER_TIER0_OPERATION_RECORDS;
                    if (valid)
                    {
                        output->records[output->record_count++] = check_vector_lower_record(X86_FORM_MOVUPS_X_XM, to, 0, from);
                    }
                }
                record.vvvv = 0;
                if (shift)
                {
                    record.rm = to;
                }
            }
            record.form = (uint16_t)form;
            // Legacy SSE's compares encode 8 predicates; AVX's other 24 have no legacy form.
            const bool compare = form == X86_FORM_CMPPS_X_XM128_IMM8 || form == X86_FORM_CMPPD_X_XM128_IMM8 || form == X86_FORM_CMPSS_X_XM32_IMM8 ||
                                 form == X86_FORM_CMPSD_X_XM64_IMM8;
            valid = valid && (!compare || (record.immediate >= 0 && record.immediate < 8));
        }
        valid = valid && output->record_count < BUSTER_TIER0_OPERATION_RECORDS;
        if (valid)
        {
            output->records[output->record_count++] = record;
        }
    }
    return valid;
}
