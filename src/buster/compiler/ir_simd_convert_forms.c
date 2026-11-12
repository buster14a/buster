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

// The floating operations at their own rounding (#1840, BUSTER_IR_INTRINSIC_FROUND), by the operation (add, sub, mul,
// div) and the lanes (floats, doubles): the 64-byte register forms with embedded rounding, the first operand in vvvv,
// the second in rm and the rounding in the decorations' bits (BUSTER_X86_DECORATION_ROUNDING).
static const uint16_t ir_simd_fround_forms[4][2] = {
    { X86_FORM_VADDPS_Z_Z_Z_ER, X86_FORM_VADDPD_Z_Z_Z_ER },
    { X86_FORM_VSUBPS_Z_Z_Z_ER, X86_FORM_VSUBPD_Z_Z_Z_ER },
    { X86_FORM_VMULPS_Z_Z_Z_ER, X86_FORM_VMULPD_Z_Z_Z_ER },
    { X86_FORM_VDIVPS_Z_Z_Z_ER, X86_FORM_VDIVPD_Z_Z_Z_ER },
};

#endif
