// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The integer lanes' conversions' and scalar shifts' x86 forms (#1840), which the typed lowering's decoder (ir_simd_typed_convert) admits
// and Tier 1's selection (tier1_select_convert_form) emits: one table for both, so that they agree. Include after
// x86_64_encoder.c; ir_simd_typed.c and tier1_select.c both include it, the first one alone defining it.
#ifndef BUSTER_IR_SIMD_CONVERT_FORMS_INCLUDED
#define BUSTER_IR_SIMD_CONVERT_FORMS_INCLUDED

// The conversions' forms (#1840): the extensions by their sign (zero-extending first) and pair of lanes (bytes to words,
// doublewords and quadwords, words to doublewords and quadwords, doublewords to quadwords) at the result's width, and the
// truncations by their mode (wrapping, signed and unsigned saturation) and pair (words to bytes, doublewords to bytes and
// words, quadwords to bytes, words and doublewords) at the source's width. Tier 1 selects the same forms
// (tier1_select_convert_form).
static const uint16_t ir_simd_typed_extensions[2][6][3] = {
    {
        { X86_FORM_VPMOVZXBW_X_XM64_EVEX, X86_FORM_VPMOVZXBW_Y_XM128_EVEX, X86_FORM_VPMOVZXBW_Z_YM256 },
        { X86_FORM_VPMOVZXBD_X_XM32_EVEX, X86_FORM_VPMOVZXBD_Y_XM64_EVEX, X86_FORM_VPMOVZXBD_Z_XM128 },
        { X86_FORM_VPMOVZXBQ_X_XM16_EVEX, X86_FORM_VPMOVZXBQ_Y_XM32_EVEX, X86_FORM_VPMOVZXBQ_Z_XM64 },
        { X86_FORM_VPMOVZXWD_X_XM64_EVEX, X86_FORM_VPMOVZXWD_Y_XM128_EVEX, X86_FORM_VPMOVZXWD_Z_YM256 },
        { X86_FORM_VPMOVZXWQ_X_XM32_EVEX, X86_FORM_VPMOVZXWQ_Y_XM64_EVEX, X86_FORM_VPMOVZXWQ_Z_XM128 },
        { X86_FORM_VPMOVZXDQ_X_XM64_EVEX, X86_FORM_VPMOVZXDQ_Y_XM128_EVEX, X86_FORM_VPMOVZXDQ_Z_YM256 },
    },
    {
        { X86_FORM_VPMOVSXBW_X_XM64_EVEX, X86_FORM_VPMOVSXBW_Y_XM128_EVEX, X86_FORM_VPMOVSXBW_Z_YM256 },
        { X86_FORM_VPMOVSXBD_X_XM32_EVEX, X86_FORM_VPMOVSXBD_Y_XM64_EVEX, X86_FORM_VPMOVSXBD_Z_XM128 },
        { X86_FORM_VPMOVSXBQ_X_XM16_EVEX, X86_FORM_VPMOVSXBQ_Y_XM32_EVEX, X86_FORM_VPMOVSXBQ_Z_XM64 },
        { X86_FORM_VPMOVSXWD_X_XM64_EVEX, X86_FORM_VPMOVSXWD_Y_XM128_EVEX, X86_FORM_VPMOVSXWD_Z_YM256 },
        { X86_FORM_VPMOVSXWQ_X_XM32_EVEX, X86_FORM_VPMOVSXWQ_Y_XM64_EVEX, X86_FORM_VPMOVSXWQ_Z_XM128 },
        { X86_FORM_VPMOVSXDQ_X_XM64_EVEX, X86_FORM_VPMOVSXDQ_Y_XM128_EVEX, X86_FORM_VPMOVSXDQ_Z_YM256 },
    },
};
static const uint16_t ir_simd_typed_truncations[3][6][3] = {
    {
        { X86_FORM_VPMOVWB_XM64_X, X86_FORM_VPMOVWB_XM128_Y, X86_FORM_VPMOVWB_YM256_Z },
        { X86_FORM_VPMOVDB_XM32_X, X86_FORM_VPMOVDB_XM64_Y, X86_FORM_VPMOVDB_XM128_Z },
        { X86_FORM_VPMOVDW_XM64_X, X86_FORM_VPMOVDW_XM128_Y, X86_FORM_VPMOVDW_YM256_Z },
        { X86_FORM_VPMOVQB_XM16_X, X86_FORM_VPMOVQB_XM32_Y, X86_FORM_VPMOVQB_XM64_Z },
        { X86_FORM_VPMOVQW_XM32_X, X86_FORM_VPMOVQW_XM64_Y, X86_FORM_VPMOVQW_XM128_Z },
        { X86_FORM_VPMOVQD_XM64_X, X86_FORM_VPMOVQD_XM128_Y, X86_FORM_VPMOVQD_YM256_Z },
    },
    {
        { X86_FORM_VPMOVSWB_XM64_X, X86_FORM_VPMOVSWB_XM128_Y, X86_FORM_VPMOVSWB_YM256_Z },
        { X86_FORM_VPMOVSDB_XM32_X, X86_FORM_VPMOVSDB_XM64_Y, X86_FORM_VPMOVSDB_XM128_Z },
        { X86_FORM_VPMOVSDW_XM64_X, X86_FORM_VPMOVSDW_XM128_Y, X86_FORM_VPMOVSDW_YM256_Z },
        { X86_FORM_VPMOVSQB_XM16_X, X86_FORM_VPMOVSQB_XM32_Y, X86_FORM_VPMOVSQB_XM64_Z },
        { X86_FORM_VPMOVSQW_XM32_X, X86_FORM_VPMOVSQW_XM64_Y, X86_FORM_VPMOVSQW_XM128_Z },
        { X86_FORM_VPMOVSQD_XM64_X, X86_FORM_VPMOVSQD_XM128_Y, X86_FORM_VPMOVSQD_YM256_Z },
    },
    {
        { X86_FORM_VPMOVUSWB_XM64_X, X86_FORM_VPMOVUSWB_XM128_Y, X86_FORM_VPMOVUSWB_YM256_Z },
        { X86_FORM_VPMOVUSDB_XM32_X, X86_FORM_VPMOVUSDB_XM64_Y, X86_FORM_VPMOVUSDB_XM128_Z },
        { X86_FORM_VPMOVUSDW_XM64_X, X86_FORM_VPMOVUSDW_XM128_Y, X86_FORM_VPMOVUSDW_YM256_Z },
        { X86_FORM_VPMOVUSQB_XM16_X, X86_FORM_VPMOVUSQB_XM32_Y, X86_FORM_VPMOVUSQB_XM64_Z },
        { X86_FORM_VPMOVUSQW_XM32_X, X86_FORM_VPMOVUSQW_XM64_Y, X86_FORM_VPMOVUSQW_XM128_Z },
        { X86_FORM_VPMOVUSQD_XM64_X, X86_FORM_VPMOVUSQD_XM128_Y, X86_FORM_VPMOVUSQD_YM256_Z },
    },
};

// A conversion's pair of lanes' bytes' log2s, by the tables' pair index: the extensions' (from, to), and the truncations'.
static const uint8_t ir_simd_typed_extension_pairs[6][2] = { { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 2 }, { 1, 3 }, { 2, 3 } };
static const uint8_t ir_simd_typed_truncation_pairs[6][2] = { { 1, 0 }, { 2, 0 }, { 2, 1 }, { 3, 0 }, { 3, 1 }, { 3, 2 } };

// The shifts by a scalar count (#1840, BUSTER_IR_INTRINSIC_SHIFT), by the shift (left, logical right, arithmetic right),
// the lanes (words, doublewords, quadwords) and the width (16, 32 and 64 bytes): by a count in an xmm register's low
// quadword, which the decoder admits (ir_simd_typed_shift) and Tier 1 emits after a vmovd of the count; and by an
// immediate, which Tier 1 emits for a constant count below the lane's bits (tier1_select_vector_shift). EVEX's forms at every
// width: VL's below 64 bytes, BW's for words.
static const uint16_t ir_simd_shift_forms[3][3][3] = {
    {
        { X86_FORM_VPSLLW_X_X_XM128_EVEX, X86_FORM_VPSLLW_Y_Y_XM128_EVEX, X86_FORM_VPSLLW_Z_Z_XM128 },
        { X86_FORM_VPSLLD_X_X_XM128_EVEX, X86_FORM_VPSLLD_Y_Y_XM128_EVEX, X86_FORM_VPSLLD_Z_Z_XM128 },
        { X86_FORM_VPSLLQ_X_X_XM128_EVEX, X86_FORM_VPSLLQ_Y_Y_XM128_EVEX, X86_FORM_VPSLLQ_Z_Z_XM128 },
    },
    {
        { X86_FORM_VPSRLW_X_X_XM128_EVEX, X86_FORM_VPSRLW_Y_Y_XM128_EVEX, X86_FORM_VPSRLW_Z_Z_XM128 },
        { X86_FORM_VPSRLD_X_X_XM128_EVEX, X86_FORM_VPSRLD_Y_Y_XM128_EVEX, X86_FORM_VPSRLD_Z_Z_XM128 },
        { X86_FORM_VPSRLQ_X_X_XM128_EVEX, X86_FORM_VPSRLQ_Y_Y_XM128_EVEX, X86_FORM_VPSRLQ_Z_Z_XM128 },
    },
    {
        { X86_FORM_VPSRAW_X_X_XM128_EVEX, X86_FORM_VPSRAW_Y_Y_XM128_EVEX, X86_FORM_VPSRAW_Z_Z_XM128 },
        { X86_FORM_VPSRAD_X_X_XM128_EVEX, X86_FORM_VPSRAD_Y_Y_XM128_EVEX, X86_FORM_VPSRAD_Z_Z_XM128 },
        { X86_FORM_VPSRAQ_X_X_XM128, X86_FORM_VPSRAQ_Y_Y_XM128, X86_FORM_VPSRAQ_Z_Z_XM128 },
    },
};
// The lane operations (#1840, BUSTER_IR_INTRINSIC_LANE_OP), by the operation (the unsigned and signed minimum and
// maximum, the unsigned and signed saturating sum and difference), the lanes (bytes to quadwords) and the width, the
// first operand in vvvv and the second in rm, X86_FORM_NONE where AVX-512 has none (saturation of doublewords and
// quadwords): which the decoder admits (ir_simd_typed_lane_op) and Tier 1 emits (tier1_select.c).
static const uint16_t ir_simd_lane_op_forms[8][4][3] = {
    {
        { X86_FORM_VPMINUB_X_X_XM128_EVEX, X86_FORM_VPMINUB_Y_Y_YM256_EVEX, X86_FORM_VPMINUB_Z_Z_ZM512 },
        { X86_FORM_VPMINUW_X_X_XM128_EVEX, X86_FORM_VPMINUW_Y_Y_YM256_EVEX, X86_FORM_VPMINUW_Z_Z_ZM512 },
        { X86_FORM_VPMINUD_X_X_XM128_EVEX, X86_FORM_VPMINUD_Y_Y_YM256_EVEX, X86_FORM_VPMINUD_Z_Z_ZM512 },
        { X86_FORM_VPMINUQ_X_X_XM128, X86_FORM_VPMINUQ_Y_Y_YM256, X86_FORM_VPMINUQ_Z_Z_ZM512 },
    },
    {
        { X86_FORM_VPMINSB_X_X_XM128_EVEX, X86_FORM_VPMINSB_Y_Y_YM256_EVEX, X86_FORM_VPMINSB_Z_Z_ZM512 },
        { X86_FORM_VPMINSW_X_X_XM128_EVEX, X86_FORM_VPMINSW_Y_Y_YM256_EVEX, X86_FORM_VPMINSW_Z_Z_ZM512 },
        { X86_FORM_VPMINSD_X_X_XM128_EVEX, X86_FORM_VPMINSD_Y_Y_YM256_EVEX, X86_FORM_VPMINSD_Z_Z_ZM512 },
        { X86_FORM_VPMINSQ_X_X_XM128, X86_FORM_VPMINSQ_Y_Y_YM256, X86_FORM_VPMINSQ_Z_Z_ZM512 },
    },
    {
        { X86_FORM_VPMAXUB_X_X_XM128_EVEX, X86_FORM_VPMAXUB_Y_Y_YM256_EVEX, X86_FORM_VPMAXUB_Z_Z_ZM512 },
        { X86_FORM_VPMAXUW_X_X_XM128_EVEX, X86_FORM_VPMAXUW_Y_Y_YM256_EVEX, X86_FORM_VPMAXUW_Z_Z_ZM512 },
        { X86_FORM_VPMAXUD_X_X_XM128_EVEX, X86_FORM_VPMAXUD_Y_Y_YM256_EVEX, X86_FORM_VPMAXUD_Z_Z_ZM512 },
        { X86_FORM_VPMAXUQ_X_X_XM128, X86_FORM_VPMAXUQ_Y_Y_YM256, X86_FORM_VPMAXUQ_Z_Z_ZM512 },
    },
    {
        { X86_FORM_VPMAXSB_X_X_XM128_EVEX, X86_FORM_VPMAXSB_Y_Y_YM256_EVEX, X86_FORM_VPMAXSB_Z_Z_ZM512 },
        { X86_FORM_VPMAXSW_X_X_XM128_EVEX, X86_FORM_VPMAXSW_Y_Y_YM256_EVEX, X86_FORM_VPMAXSW_Z_Z_ZM512 },
        { X86_FORM_VPMAXSD_X_X_XM128_EVEX, X86_FORM_VPMAXSD_Y_Y_YM256_EVEX, X86_FORM_VPMAXSD_Z_Z_ZM512 },
        { X86_FORM_VPMAXSQ_X_X_XM128, X86_FORM_VPMAXSQ_Y_Y_YM256, X86_FORM_VPMAXSQ_Z_Z_ZM512 },
    },
    {
        { X86_FORM_VPADDUSB_X_X_XM128_EVEX, X86_FORM_VPADDUSB_Y_Y_YM256_EVEX, X86_FORM_VPADDUSB_Z_Z_ZM512 },
        { X86_FORM_VPADDUSW_X_X_XM128_EVEX, X86_FORM_VPADDUSW_Y_Y_YM256_EVEX, X86_FORM_VPADDUSW_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
    },
    {
        { X86_FORM_VPADDSB_X_X_XM128_EVEX, X86_FORM_VPADDSB_Y_Y_YM256_EVEX, X86_FORM_VPADDSB_Z_Z_ZM512 },
        { X86_FORM_VPADDSW_X_X_XM128_EVEX, X86_FORM_VPADDSW_Y_Y_YM256_EVEX, X86_FORM_VPADDSW_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
    },
    {
        { X86_FORM_VPSUBUSB_X_X_XM128_EVEX, X86_FORM_VPSUBUSB_Y_Y_YM256_EVEX, X86_FORM_VPSUBUSB_Z_Z_ZM512 },
        { X86_FORM_VPSUBUSW_X_X_XM128_EVEX, X86_FORM_VPSUBUSW_Y_Y_YM256_EVEX, X86_FORM_VPSUBUSW_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
    },
    {
        { X86_FORM_VPSUBSB_X_X_XM128_EVEX, X86_FORM_VPSUBSB_Y_Y_YM256_EVEX, X86_FORM_VPSUBSB_Z_Z_ZM512 },
        { X86_FORM_VPSUBSW_X_X_XM128_EVEX, X86_FORM_VPSUBSW_Y_Y_YM256_EVEX, X86_FORM_VPSUBSW_Z_Z_ZM512 },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
    },
};
static const uint16_t ir_simd_shift_immediate_forms[3][3][3] = {
    {
        { X86_FORM_VPSLLW_X_XM128_IMM8, X86_FORM_VPSLLW_Y_YM256_IMM8, X86_FORM_VPSLLW_Z_ZM512_IMM8 },
        { X86_FORM_VPSLLD_X_XM128_IMM8, X86_FORM_VPSLLD_Y_YM256_IMM8, X86_FORM_VPSLLD_Z_ZM512_IMM8 },
        { X86_FORM_VPSLLQ_X_XM128_IMM8, X86_FORM_VPSLLQ_Y_YM256_IMM8, X86_FORM_VPSLLQ_Z_ZM512_IMM8 },
    },
    {
        { X86_FORM_VPSRLW_X_XM128_IMM8, X86_FORM_VPSRLW_Y_YM256_IMM8, X86_FORM_VPSRLW_Z_ZM512_IMM8 },
        { X86_FORM_VPSRLD_X_XM128_IMM8, X86_FORM_VPSRLD_Y_YM256_IMM8, X86_FORM_VPSRLD_Z_ZM512_IMM8 },
        { X86_FORM_VPSRLQ_X_XM128_IMM8, X86_FORM_VPSRLQ_Y_YM256_IMM8, X86_FORM_VPSRLQ_Z_ZM512_IMM8 },
    },
    {
        { X86_FORM_VPSRAW_X_XM128_IMM8, X86_FORM_VPSRAW_Y_YM256_IMM8, X86_FORM_VPSRAW_Z_ZM512_IMM8 },
        { X86_FORM_VPSRAD_X_XM128_IMM8, X86_FORM_VPSRAD_Y_YM256_IMM8, X86_FORM_VPSRAD_Z_ZM512_IMM8 },
        { X86_FORM_VPSRAQ_X_XM128_IMM8, X86_FORM_VPSRAQ_Y_YM256_IMM8, X86_FORM_VPSRAQ_Z_ZM512_IMM8 },
    },
};
// The permutes of two tables (#1840, BUSTER_IR_INTRINSIC_PERMUTE2), by their lanes' bytes' log2 and width (16, 32 or 64
// bytes): vpermt2b to vpermt2q, whose destination is the first table and which read the indices from vvvv and the second
// table from rm.
static const uint16_t ir_simd_permute2_forms[4][3] = {
    { X86_FORM_VPERMT2B_X_X_XM128, X86_FORM_VPERMT2B_Y_Y_YM256, X86_FORM_VPERMT2B_Z_Z_ZM512 },
    { X86_FORM_VPERMT2W_X_X_XM128, X86_FORM_VPERMT2W_Y_Y_YM256, X86_FORM_VPERMT2W_Z_Z_ZM512 },
    { X86_FORM_VPERMT2D_X_X_XM128, X86_FORM_VPERMT2D_Y_Y_YM256, X86_FORM_VPERMT2D_Z_Z_ZM512 },
    { X86_FORM_VPERMT2Q_X_X_XM128, X86_FORM_VPERMT2Q_Y_Y_YM256, X86_FORM_VPERMT2Q_Z_Z_ZM512 },
};
// The bitwise functions of three vectors (#1840, BUSTER_IR_INTRINSIC_TERNLOG), by their writemask's lanes (doublewords,
// quadwords) and width: vpternlogd and vpternlogq, the destination the first operand, the second in vvvv and the third in
// rm. Without a writemask they compute the same bits.
static const uint16_t ir_simd_ternlog_forms[2][3] = {
    { X86_FORM_VPTERNLOGD_X_X_XM128_IMM8, X86_FORM_VPTERNLOGD_Y_Y_YM256_IMM8, X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8 },
    { X86_FORM_VPTERNLOGQ_X_X_XM128_IMM8, X86_FORM_VPTERNLOGQ_Y_Y_YM256_IMM8, X86_FORM_VPTERNLOGQ_Z_Z_ZM512_IMM8 },
};
// The compresses and expands of a vector's lanes into a register (#1840, BUSTER_IR_INTRINSIC_COMPRESS and _EXPAND), by
// their lanes' bytes' log2 and width: vpcompressb to vpcompressq, the source in reg and the destination in rm, and
// vpexpandb to vpexpandq, the destination in reg and the source in rm, each merged under a writemask; and the move of a
// whole vector at each width that the front end's rows end with.
static const uint16_t ir_simd_compress_forms[4][3] = {
    { X86_FORM_VPCOMPRESSB_XM128_X, X86_FORM_VPCOMPRESSB_YM256_Y, X86_FORM_VPCOMPRESSB_ZM512_Z },
    { X86_FORM_VPCOMPRESSW_XM128_X, X86_FORM_VPCOMPRESSW_YM256_Y, X86_FORM_VPCOMPRESSW_ZM512_Z },
    { X86_FORM_VPCOMPRESSD_XM128_X, X86_FORM_VPCOMPRESSD_YM256_Y, X86_FORM_VPCOMPRESSD_ZM512_Z },
    { X86_FORM_VPCOMPRESSQ_XM128_X, X86_FORM_VPCOMPRESSQ_YM256_Y, X86_FORM_VPCOMPRESSQ_ZM512_Z },
};
static const uint16_t ir_simd_expand_forms[4][3] = {
    { X86_FORM_VPEXPANDB_X_XM128, X86_FORM_VPEXPANDB_Y_YM256, X86_FORM_VPEXPANDB_Z_ZM512 },
    { X86_FORM_VPEXPANDW_X_XM128, X86_FORM_VPEXPANDW_Y_YM256, X86_FORM_VPEXPANDW_Z_ZM512 },
    { X86_FORM_VPEXPANDD_X_XM128, X86_FORM_VPEXPANDD_Y_YM256, X86_FORM_VPEXPANDD_Z_ZM512 },
    { X86_FORM_VPEXPANDQ_X_XM128, X86_FORM_VPEXPANDQ_Y_YM256, X86_FORM_VPEXPANDQ_Z_ZM512 },
};
static const uint16_t ir_simd_whole_moves[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
// The AES rounds (#1840, BUSTER_IR_INTRINSIC_AES), by round (0 vaesenc, 1 vaesenclast) and width: the EVEX forms, and
// the VEX forms at 16 and 32 bytes (X86_FORM_NONE at 64), the state in reg's vvvv and the round key in rm.
static const uint16_t ir_simd_aes_forms[2][3] = {
    { X86_FORM_VAESENC_X_X_XM128_EVEX, X86_FORM_VAESENC_Y_Y_YM256_EVEX, X86_FORM_VAESENC_Z_Z_ZM512 },
    { X86_FORM_VAESENCLAST_X_X_XM128_EVEX, X86_FORM_VAESENCLAST_Y_Y_YM256_EVEX, X86_FORM_VAESENCLAST_Z_Z_ZM512 },
};
static const uint16_t ir_simd_aes_vex_forms[2][3] = {
    { X86_FORM_VAESENC_X_X_XM128, X86_FORM_VAESENC_Y_Y_YM256, X86_FORM_NONE },
    { X86_FORM_VAESENCLAST_X_X_XM128, X86_FORM_VAESENCLAST_Y_Y_YM256, X86_FORM_NONE },
};
// vpermi2b to vpermi2q, the same permutes whose destination is the indices, the first table in vvvv and the second in rm
// (Tier 1's where the destination's register holds the indices).
static const uint16_t ir_simd_permute2_index_forms[4][3] = {
    { X86_FORM_VPERMI2B_X_X_XM128, X86_FORM_VPERMI2B_Y_Y_YM256, X86_FORM_VPERMI2B_Z_Z_ZM512 },
    { X86_FORM_VPERMI2W_X_X_XM128, X86_FORM_VPERMI2W_Y_Y_YM256, X86_FORM_VPERMI2W_Z_Z_ZM512 },
    { X86_FORM_VPERMI2D_X_X_XM128, X86_FORM_VPERMI2D_Y_Y_YM256, X86_FORM_VPERMI2D_Z_Z_ZM512 },
    { X86_FORM_VPERMI2Q_X_X_XM128, X86_FORM_VPERMI2Q_Y_Y_YM256, X86_FORM_VPERMI2Q_Z_Z_ZM512 },
};

// The masked loads' and stores' moves (#1840, IR_MASKED_LOAD and _STORE) by kind (vmovdqu8 to vmovdqu64, vmovdqa32,
// vmovdqa64, vmovups, vmovupd, vmovaps, vmovapd), load then store, at each width: the front end's rows' forms, which the
// decoder admits (ir_simd_typed_masked_memory); Tier 1 selects the first four, vmovdqu8 to vmovdqu64, by the lanes.
static const uint16_t ir_simd_masked_moves[10][2][3] = {
    { { X86_FORM_VMOVDQU8_X_XM128, X86_FORM_VMOVDQU8_Y_YM256, X86_FORM_VMOVDQU8_Z_ZM512 }, { X86_FORM_VMOVDQU8_XM128_X, X86_FORM_VMOVDQU8_YM256_Y, X86_FORM_VMOVDQU8_ZM512_Z } },
    { { X86_FORM_VMOVDQU16_X_XM128, X86_FORM_VMOVDQU16_Y_YM256, X86_FORM_VMOVDQU16_Z_ZM512 }, { X86_FORM_VMOVDQU16_XM128_X, X86_FORM_VMOVDQU16_YM256_Y, X86_FORM_VMOVDQU16_ZM512_Z } },
    { { X86_FORM_VMOVDQU32_X_XM128, X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_Z_ZM512 }, { X86_FORM_VMOVDQU32_XM128_X, X86_FORM_VMOVDQU32_YM256_Y, X86_FORM_VMOVDQU32_ZM512_Z } },
    { { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 }, { X86_FORM_VMOVDQU64_XM128_X, X86_FORM_VMOVDQU64_YM256_Y, X86_FORM_VMOVDQU64_ZM512_Z } },
    { { X86_FORM_VMOVDQA32_X_XM128, X86_FORM_VMOVDQA32_Y_YM256, X86_FORM_VMOVDQA32_Z_ZM512 }, { X86_FORM_VMOVDQA32_XM128_X, X86_FORM_VMOVDQA32_YM256_Y, X86_FORM_VMOVDQA32_ZM512_Z } },
    { { X86_FORM_VMOVDQA64_X_XM128, X86_FORM_VMOVDQA64_Y_YM256, X86_FORM_VMOVDQA64_Z_ZM512 }, { X86_FORM_VMOVDQA64_XM128_X, X86_FORM_VMOVDQA64_YM256_Y, X86_FORM_VMOVDQA64_ZM512_Z } },
    { { X86_FORM_VMOVUPS_X_XM128_EVEX, X86_FORM_VMOVUPS_Y_YM256_EVEX, X86_FORM_VMOVUPS_Z_ZM512 }, { X86_FORM_VMOVUPS_XM128_X_EVEX, X86_FORM_VMOVUPS_YM256_Y_EVEX, X86_FORM_VMOVUPS_ZM512_Z } },
    { { X86_FORM_VMOVUPD_X_XM128_EVEX, X86_FORM_VMOVUPD_Y_YM256_EVEX, X86_FORM_VMOVUPD_Z_ZM512 }, { X86_FORM_VMOVUPD_XM128_X_EVEX, X86_FORM_VMOVUPD_YM256_Y_EVEX, X86_FORM_VMOVUPD_ZM512_Z } },
    { { X86_FORM_VMOVAPS_X_XM128_EVEX, X86_FORM_VMOVAPS_Y_YM256_EVEX, X86_FORM_VMOVAPS_Z_ZM512 }, { X86_FORM_VMOVAPS_XM128_X_EVEX, X86_FORM_VMOVAPS_YM256_Y_EVEX, X86_FORM_VMOVAPS_ZM512_Z } },
    { { X86_FORM_VMOVAPD_X_XM128_EVEX, X86_FORM_VMOVAPD_Y_YM256_EVEX, X86_FORM_VMOVAPD_Z_ZM512 }, { X86_FORM_VMOVAPD_XM128_X_EVEX, X86_FORM_VMOVAPD_YM256_Y_EVEX, X86_FORM_VMOVAPD_ZM512_Z } },
};

// One lane of a vector and its lanes' sign bits (#1840, BUSTER_IR_INTRINSIC_LANE_EXTRACT, _LANE_INSERT and _MOVEMASK), by
// the lane's bytes' log2: vpextrb to vpextrq of an xmm register's lane (vpextrw's destination in reg, the others' in rm),
// vpinsrb to vpinsrq into one (reg and vvvv the vector, rm the scalar), and vpmovmskb, vmovmskps and vmovmskpd of an xmm
// or a ymm register (X86_FORM_NONE for words, which have none); the 16-byte part's moves of a ymm and a zmm register past
// the low one (vextracti32x4, vinserti32x4).
static const uint16_t ir_simd_lane_extract_forms[4] = {
    X86_FORM_VPEXTRB_RM32_X_IMM8_EVEX,
    X86_FORM_VPEXTRW_R32_X_IMM8_EVEX,
    X86_FORM_VPEXTRD_RM32_X_IMM8_EVEX,
    X86_FORM_VPEXTRQ_RM64_X_IMM8_EVEX,
};
static const uint16_t ir_simd_lane_insert_forms[4] = {
    X86_FORM_VPINSRB_X_X_RM32_IMM8_EVEX,
    X86_FORM_VPINSRW_X_X_RM32_IMM8_EVEX,
    X86_FORM_VPINSRD_X_X_RM32_IMM8_EVEX,
    X86_FORM_VPINSRQ_X_X_RM64_IMM8_EVEX,
};
static const uint16_t ir_simd_movemask_forms[4][2] = {
    { X86_FORM_VPMOVMSKB_R32_X, X86_FORM_VPMOVMSKB_R32_Y },
    { X86_FORM_NONE, X86_FORM_NONE },
    { X86_FORM_VMOVMSKPS_R32_X, X86_FORM_VMOVMSKPS_R32_Y },
    { X86_FORM_VMOVMSKPD_R32_X, X86_FORM_VMOVMSKPD_R32_Y },
};
static const uint16_t ir_simd_part_extract_forms[2] = { X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, X86_FORM_VEXTRACTI32X4_XM128_Z_IMM8 };
static const uint16_t ir_simd_part_insert_forms[2] = { X86_FORM_VINSERTI32X4_Y_Y_XM128_IMM8, X86_FORM_VINSERTI32X4_Z_Z_XM128_IMM8 };

// A reduction's halvings (#1840, BUSTER_IR_INTRINSIC_REDUCE), by the operation past the minima and maxima (whose forms
// are ir_simd_lane_op_forms'), the lanes (doublewords, quadwords) and the width, the halves in vvvv and rm, as
// check_vector_elementwise_forms gives clang's __builtin_reduce_add, _mul, _and, _or and _xor (the bitwise ones the
// quadwords' forms whatever the lanes).
static const uint16_t ir_simd_reduce_forms[5][2][3] = {
    { { X86_FORM_VPADDD_X_X_XM128_EVEX, X86_FORM_VPADDD_Y_Y_YM256_EVEX, X86_FORM_VPADDD_Z_Z_ZM512 }, { X86_FORM_VPADDQ_X_X_XM128_EVEX, X86_FORM_VPADDQ_Y_Y_YM256_EVEX, X86_FORM_VPADDQ_Z_Z_ZM512 } },
    { { X86_FORM_VPMULLD_X_X_XM128_EVEX, X86_FORM_VPMULLD_Y_Y_YM256_EVEX, X86_FORM_VPMULLD_Z_Z_ZM512 }, { X86_FORM_VPMULLQ_X_X_XM128, X86_FORM_VPMULLQ_Y_Y_YM256, X86_FORM_VPMULLQ_Z_Z_ZM512 } },
    { { X86_FORM_VPANDQ_X_X_XM128, X86_FORM_VPANDQ_Y_Y_YM256, X86_FORM_VPANDQ_Z_Z_ZM512 }, { X86_FORM_VPANDQ_X_X_XM128, X86_FORM_VPANDQ_Y_Y_YM256, X86_FORM_VPANDQ_Z_Z_ZM512 } },
    { { X86_FORM_VPORQ_X_X_XM128, X86_FORM_VPORQ_Y_Y_YM256, X86_FORM_VPORQ_Z_Z_ZM512 }, { X86_FORM_VPORQ_X_X_XM128, X86_FORM_VPORQ_Y_Y_YM256, X86_FORM_VPORQ_Z_Z_ZM512 } },
    { { X86_FORM_VPXORQ_X_X_XM128, X86_FORM_VPXORQ_Y_Y_YM256, X86_FORM_VPXORQ_Z_Z_ZM512 }, { X86_FORM_VPXORQ_X_X_XM128, X86_FORM_VPXORQ_Y_Y_YM256, X86_FORM_VPXORQ_Z_Z_ZM512 } },
};

// A reduction's form for the operation (BUSTER_IR_REDUCE_*), the lanes' bytes' log2 (2 or 3) and the width (16, 32 or
// 64 bytes: 0, 1 or 2).
static uint16_t ir_simd_reduce_form(uint32_t operation, uint32_t size, uint32_t width)
{
    return operation < 4 ? ir_simd_lane_op_forms[operation][size][width] : ir_simd_reduce_forms[operation - 4][size - 2][width];
}

// The lanes' unary operations (#1840, BUSTER_IR_INTRINSIC_LANE_UNARY), by the operation (population count, leading zero
// count, conflicts), the lanes (bytes to quadwords) and the width, the destination in reg and the source in rm:
// vpopcntb to vpopcntq (BITALG's bytes and words, VPOPCNTDQ's doublewords and quadwords), AVX-512CD's vplzcntd,
// vplzcntq, vpconflictd and vpconflictq, X86_FORM_NONE where AVX-512 has none.
static const uint16_t ir_simd_lane_unary_forms[3][4][3] = {
    {
        { X86_FORM_VPOPCNTB_X_XM128, X86_FORM_VPOPCNTB_Y_YM256, X86_FORM_VPOPCNTB_Z_ZM512 },
        { X86_FORM_VPOPCNTW_X_XM128, X86_FORM_VPOPCNTW_Y_YM256, X86_FORM_VPOPCNTW_Z_ZM512 },
        { X86_FORM_VPOPCNTD_X_XM128, X86_FORM_VPOPCNTD_Y_YM256, X86_FORM_VPOPCNTD_Z_ZM512 },
        { X86_FORM_VPOPCNTQ_X_XM128, X86_FORM_VPOPCNTQ_Y_YM256, X86_FORM_VPOPCNTQ_Z_ZM512 },
    },
    {
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_VPLZCNTD_X_XM128, X86_FORM_VPLZCNTD_Y_YM256, X86_FORM_VPLZCNTD_Z_ZM512 },
        { X86_FORM_VPLZCNTQ_X_XM128, X86_FORM_VPLZCNTQ_Y_YM256, X86_FORM_VPLZCNTQ_Z_ZM512 },
    },
    {
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE },
        { X86_FORM_VPCONFLICTD_X_XM128, X86_FORM_VPCONFLICTD_Y_YM256, X86_FORM_VPCONFLICTD_Z_ZM512 },
        { X86_FORM_VPCONFLICTQ_X_XM128, X86_FORM_VPCONFLICTQ_Y_YM256, X86_FORM_VPCONFLICTQ_Z_ZM512 },
    },
};
// The floating operations at their own rounding (#1840, BUSTER_IR_INTRINSIC_FROUND), by the operation (add, sub, mul,
// div) and the lanes (floats, doubles): the 64-byte register forms with embedded rounding, the first operand in vvvv,
// the second in rm and the rounding in the decorations' bits (BUSTER_X86_DECORATION_ROUNDING).
// The truncating conversions of lane 0 (#1840, BUSTER_IR_INTRINSIC_CVTT), by the lane (float, double) and the result
// (32, 64 bits): the EVEX forms, the general register in reg and the vector in rm.
static const uint16_t ir_simd_cvtt_forms[2][2] = {
    { X86_FORM_VCVTTSS2SI_R32_XM32_EVEX, X86_FORM_VCVTTSS2SI_R64_XM32_EVEX },
    { X86_FORM_VCVTTSD2SI_R32_XM64_EVEX, X86_FORM_VCVTTSD2SI_R64_XM64_EVEX },
};
static const uint16_t ir_simd_fround_forms[4][2] = {
    { X86_FORM_VADDPS_Z_Z_Z_ER, X86_FORM_VADDPD_Z_Z_Z_ER },
    { X86_FORM_VSUBPS_Z_Z_Z_ER, X86_FORM_VSUBPD_Z_Z_Z_ER },
    { X86_FORM_VMULPS_Z_Z_Z_ER, X86_FORM_VMULPD_Z_Z_Z_ER },
    { X86_FORM_VDIVPS_Z_Z_Z_ER, X86_FORM_VDIVPD_Z_Z_Z_ER },
};

// The byte shuffles by an immediate (#1840): vpshufd's doublewords, vpshuflw's low and vpshufhw's high words and vpslldq's
// and vpsrldq's byte shifts move the same bytes within every 16-byte lane, so each is the IR's shuffle (vpshufb) of its
// vector by a constant, their indices (ir_simd_shuffle_immediate_indices). Their EVEX forms by the kind and the width,
// each of the source in rm into the destination in reg, the byte shifts' in vvvv (their reg is the opcode's extension):
// the typed lowering's decoder (ir_simd_typed_shuffle_immediate) admits their rows, and Tier 1's selection
// (tier1_select_shuffle_immediate) matches a shuffle by a constant back to one (ir_simd_shuffle_immediate_match).
#define BUSTER_IR_SHUFFLE_IMMEDIATE_DWORDS 0u
#define BUSTER_IR_SHUFFLE_IMMEDIATE_LOW_WORDS 1u
#define BUSTER_IR_SHUFFLE_IMMEDIATE_HIGH_WORDS 2u
#define BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES 3u
#define BUSTER_IR_SHUFFLE_IMMEDIATE_RIGHT_BYTES 4u
#define BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS 5u
// A 16-byte lane's bytes, and a vpshufb index's zeroing bit.
#define BUSTER_IR_SHUFFLE_LANE_BYTES 16u
#define BUSTER_IR_SHUFFLE_ZERO 0x80u
static const uint16_t ir_simd_shuffle_immediate_forms[BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS][3] = {
    { X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX, X86_FORM_VPSHUFD_Y_YM256_IMM8_EVEX, X86_FORM_VPSHUFD_Z_ZM512_IMM8 },
    { X86_FORM_VPSHUFLW_X_XM128_IMM8_EVEX, X86_FORM_VPSHUFLW_Y_YM256_IMM8_EVEX, X86_FORM_VPSHUFLW_Z_ZM512_IMM8 },
    { X86_FORM_VPSHUFHW_X_XM128_IMM8_EVEX, X86_FORM_VPSHUFHW_Y_YM256_IMM8_EVEX, X86_FORM_VPSHUFHW_Z_ZM512_IMM8 },
    { X86_FORM_VPSLLDQ_X_XM128_IMM8, X86_FORM_VPSLLDQ_Y_YM256_IMM8, X86_FORM_VPSLLDQ_Z_ZM512_IMM8 },
    { X86_FORM_VPSRLDQ_X_XM128_IMM8, X86_FORM_VPSRLDQ_Y_YM256_IMM8, X86_FORM_VPSRLDQ_Z_ZM512_IMM8 },
};

// A byte shuffle by an immediate's indices in each 16-byte lane (#1840), vpshufb's: index i names the lane's byte that
// result byte i takes, or is BUSTER_IR_SHUFFLE_ZERO for a zero. The doublewords' and the words' immediates name a source
// by 2 bits each; a byte shift's moves the bytes up (left) or down by the immediate, 16 or more zeroing the lane.
static void ir_simd_shuffle_immediate_indices(uint32_t kind, uint32_t immediate, uint8_t* indices)
{
    for (uint32_t i = 0; i < BUSTER_IR_SHUFFLE_LANE_BYTES; i += 1)
    {
        const uint32_t word = i >> 1;
        const uint32_t dwords = ((immediate >> (2 * (i >> 2))) & 3) * 4 + (i & 3);
        const uint32_t low_words = word < 4 ? ((immediate >> (2 * word)) & 3) * 2 + (i & 1) : i;
        const uint32_t high_words = word >= 4 ? 8 + ((immediate >> (2 * (word - 4))) & 3) * 2 + (i & 1) : i;
        const uint32_t left = immediate <= i ? i - immediate : BUSTER_IR_SHUFFLE_ZERO;
        const uint32_t right = i + immediate < BUSTER_IR_SHUFFLE_LANE_BYTES ? i + immediate : BUSTER_IR_SHUFFLE_ZERO;
        const uint32_t index = kind == BUSTER_IR_SHUFFLE_IMMEDIATE_DWORDS     ? dwords :
                               kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LOW_WORDS  ? low_words :
                               kind == BUSTER_IR_SHUFFLE_IMMEDIATE_HIGH_WORDS ? high_words :
                               kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES ? left :
                                                                                right;
        indices[i] = (uint8_t)index;
    }
}

// Which byte shuffle by an immediate (#1840) a vpshufb by the constant `bytes` of `width` bytes (16, 32 or 64) is, as
// vpshufb reads an index (its zeroing bit and low 4 bits): every 16-byte lane's indices the same, and those of the first
// kind, in BUSTER_IR_SHUFFLE_IMMEDIATE's order, at the immediate the first lane's indices name for it; false for any
// other indices. The immediate form loads no constant, and on Zen 5 vpshufd and the words' shuffles run 4 per cycle
// against vpshufb's 2, at the same 2 cycles' latency. This runs once for a shuffle node whose indices are a constant, a few
// per function, on 64 bytes at most; it stays scalar.
static bool ir_simd_shuffle_immediate_match(const uint8_t* bytes, uint32_t width, uint32_t* kind, uint32_t* immediate)
{
    uint8_t lane[BUSTER_IR_SHUFFLE_LANE_BYTES];
    bool uniform = width == 16 || width == 32 || width == 64;
    for (uint32_t i = 0; i < BUSTER_IR_SHUFFLE_LANE_BYTES; i += 1)
    {
        lane[i] = (uint8_t)((bytes[i] & BUSTER_IR_SHUFFLE_ZERO) != 0 ? BUSTER_IR_SHUFFLE_ZERO : bytes[i] & 15);
    }
    for (uint32_t i = BUSTER_IR_SHUFFLE_LANE_BYTES; uniform && i < width; i += 1)
    {
        const uint32_t index = (bytes[i] & BUSTER_IR_SHUFFLE_ZERO) != 0 ? BUSTER_IR_SHUFFLE_ZERO : bytes[i] & 15u;
        uniform = index == lane[i % BUSTER_IR_SHUFFLE_LANE_BYTES];
    }
    // Each kind's immediate as the first lane names it: its doublewords' and words' first bytes' sources, and the byte
    // shifts' zeros at the lane's bottom and top.
    uint32_t candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS] = { 0, 0, 0, 0, 0 };
    for (uint32_t j = 0; j < 4; j += 1)
    {
        candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_DWORDS] |= ((uint32_t)(lane[4 * j] >> 2) & 3) << (2 * j);
        candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_LOW_WORDS] |= ((uint32_t)(lane[2 * j] >> 1) & 3) << (2 * j);
        candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_HIGH_WORDS] |= ((uint32_t)(lane[8 + 2 * j] >> 1) & 3) << (2 * j);
    }
    bool bottom = true;
    bool top = true;
    for (uint32_t i = 0; i < BUSTER_IR_SHUFFLE_LANE_BYTES; i += 1)
    {
        bottom = bottom && lane[i] == BUSTER_IR_SHUFFLE_ZERO;
        top = top && lane[BUSTER_IR_SHUFFLE_LANE_BYTES - 1 - i] == BUSTER_IR_SHUFFLE_ZERO;
        candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES] += bottom ? 1u : 0u;
        candidates[BUSTER_IR_SHUFFLE_IMMEDIATE_RIGHT_BYTES] += top ? 1u : 0u;
    }
    bool found = false;
    for (uint32_t k = 0; uniform && !found && k < BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS; k += 1)
    {
        uint8_t expected[BUSTER_IR_SHUFFLE_LANE_BYTES];
        ir_simd_shuffle_immediate_indices(k, candidates[k], expected);
        found = memcmp(expected, lane, sizeof(lane)) == 0;
        *kind = found ? k : *kind;
        *immediate = found ? candidates[k] : *immediate;
    }
    return found;
}

#endif
