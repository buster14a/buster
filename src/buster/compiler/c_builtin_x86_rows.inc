// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared source-lowering inventory (#806), originally in c_check_program_x86.c. The caller supplies
// BUSTER_CBUILTIN_X86_ROW: lowering retains each full row; feature queries retain only its exact name.
// Intentionally no include guard: both consumers expand the same rules, without an encoder dependency in the query.

#define BUSTER_CHECK_X86_RULE(name, family, element, bytes, is_signed, swap, immediate, form, features) \
    BUSTER_CBUILTIN_X86_ROW(name, sizeof(name) - 1, family, element, bytes, is_signed, swap, immediate, form, 0, false, features)
// A rule with another vector's bytes, or KBINARY's zero test.
#define BUSTER_CHECK_X86_RULE_OTHER(name, family, element, bytes, swap, immediate, form, other, features) \
    BUSTER_CBUILTIN_X86_ROW(name, sizeof(name) - 1, family, element, bytes, false, swap, immediate, form, other, false, features)
#define BUSTER_CHECK_X86_RULE_MASK(name, family, bytes, form, zero_test, features) \
    BUSTER_CBUILTIN_X86_ROW(name, sizeof(name) - 1, family, 1, bytes, false, false, family == CHECK_X86_LOWER_KSHIFT ? 1 : BUSTER_CHECK_X86_NO_IMMEDIATE, form, 0, zero_test, features)
// A rule of one width in short: its name past "__builtin_ia32_", its family's and its forms' names past their prefixes,
// with another vector's bytes (and `swap`), and with a rounding control (its last argument) and the form it picks.
#define BUSTER_CHECK_X86_ONE(name, family, element, bytes, form, features) \
    BUSTER_CHECK_X86_RULE("__builtin_ia32_" name, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, features)
#define BUSTER_CHECK_X86_ONE_OTHER(name, family, element, bytes, swap, form, other, features) \
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_" name, CHECK_X86_LOWER_##family, element, bytes, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, other, \
                                features)
#define BUSTER_CHECK_X86_ROUNDED(name, family, element, bytes, form, other, features, round_form, rounding) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, other, false, features, X86_FORM_##round_form, CHECK_X86_ROUND_##rounding, 0)
// A BINARY rule whose form's immediate is `fixed`.
#define BUSTER_CHECK_X86_RULE_FIXED(name, element, bytes, form, fixed, features) \
    BUSTER_CBUILTIN_X86_ROW(name, sizeof(name) - 1, CHECK_X86_LOWER_BINARY, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, form, 0, false, features, 0, 0, fixed)
// A family of an SSE builtin's name at 16 bytes, which has no width in it, and of its name "256" at 32: `x` and `y` are
// its forms' suffixes.
#define BUSTER_CHECK_X86_SSE(name, family, element, swap, form, x, y, features) \
    BUSTER_CHECK_X86_RULE(name, family, element, 16, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##x, features) \
    BUSTER_CHECK_X86_RULE(name "256", family, element, 32, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##y, features)
// The SSE compares of a kind (ps, pd, ss or sd) by each predicate their names spell, VCMP's of that predicate.
#define BUSTER_CHECK_X86_PREDICATES(kind, element, form) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpeq" kind, element, 16, X86_FORM_##form, 0, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmplt" kind, element, 16, X86_FORM_##form, 1, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmple" kind, element, 16, X86_FORM_##form, 2, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpunord" kind, element, 16, X86_FORM_##form, 3, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpneq" kind, element, 16, X86_FORM_##form, 4, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpnlt" kind, element, 16, X86_FORM_##form, 5, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpnle" kind, element, 16, X86_FORM_##form, 6, 0) \
    BUSTER_CHECK_X86_RULE_FIXED("__builtin_ia32_cmpord" kind, element, 16, X86_FORM_##form, 7, 0)
// The masked families at 16, 32 and 64 bytes, of each element size.
#define BUSTER_CHECK_X86_WIDTHS(prefix, suffix, family, element, is_signed, immediate) \
    BUSTER_CHECK_X86_RULE(prefix "128" suffix, family, element, 16, is_signed, false, immediate, 0, 0) \
    BUSTER_CHECK_X86_RULE(prefix "256" suffix, family, element, 32, is_signed, false, immediate, 0, 0) \
    BUSTER_CHECK_X86_RULE(prefix "512" suffix, family, element, 64, is_signed, false, immediate, 0, 0)
#define BUSTER_CHECK_X86_ELEMENTS(prefix, b, w, d, q, suffix, family, is_signed, immediate) \
    BUSTER_CHECK_X86_WIDTHS(prefix b, suffix, family, 1, is_signed, immediate) \
    BUSTER_CHECK_X86_WIDTHS(prefix w, suffix, family, 2, is_signed, immediate) \
    BUSTER_CHECK_X86_WIDTHS(prefix d, suffix, family, 4, is_signed, immediate) \
    BUSTER_CHECK_X86_WIDTHS(prefix q, suffix, family, 8, is_signed, immediate)
// A family of one form name at 16, 32 and 64 bytes: `x`, `y` and `z` are its forms' suffixes.
#define BUSTER_CHECK_X86_FORMS(name, family, element, swap, form, x, y, z, features) \
    BUSTER_CHECK_X86_RULE(name "128", family, element, 16, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##x, features) \
    BUSTER_CHECK_X86_RULE(name "256", family, element, 32, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##y, features) \
    BUSTER_CHECK_X86_RULE(name "512", family, element, 64, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##z, features)
// The same with a suffix after the width (`_mask`).
#define BUSTER_CHECK_X86_SUFFIXED(name, suffix, family, element, swap, form, x, y, z, features) \
    BUSTER_CHECK_X86_RULE(name "128" suffix, family, element, 16, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##x, features) \
    BUSTER_CHECK_X86_RULE(name "256" suffix, family, element, 32, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##y, features) \
    BUSTER_CHECK_X86_RULE(name "512" suffix, family, element, 64, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##z, features)
#define BUSTER_CHECK_X86_BINARIES(name, form, swap, features) \
    BUSTER_CHECK_X86_FORMS(name, CHECK_X86_LOWER_BINARY, 1, swap, form, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512, features)
#define BUSTER_CHECK_X86_SHIFTS(name, form, element, features) \
    BUSTER_CHECK_X86_FORMS(name, CHECK_X86_LOWER_SHIFT, element, false, form, X_X_XM128_EVEX, Y_Y_XM128_EVEX, Z_Z_XM128, features)
// A truncation under a mask of `bytes` bytes into a result of `result` bytes (TRUNCATE's `other`), and the stores of
// one at each width, whose forms are the register ones into memory.
#define BUSTER_CHECK_X86_TRUNCATION(name, element, bytes, form, result, features) \
    BUSTER_CHECK_X86_RULE_OTHER(name "_mask", CHECK_X86_LOWER_TRUNCATE, element, bytes, false, BUSTER_CHECK_X86_NO_IMMEDIATE, form, result, features)
#define BUSTER_CHECK_X86_TRUNCATION_STORES(name, element, form, x, y, z, features) \
    BUSTER_CHECK_X86_RULE_OTHER(name "128mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, element, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##x, 0, \
                                features) \
    BUSTER_CHECK_X86_RULE_OTHER(name "256mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, element, 32, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##y, 0, \
                                features) \
    BUSTER_CHECK_X86_RULE_OTHER(name "512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, element, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form##_##z, 0, \
                                features)
// A truncation's three widths, `pair` its elements (`db`), and its stores: the 16- and 32-byte ones' results are 16 bytes,
// and the 64-byte one's `wide` bytes.
#define BUSTER_CHECK_X86_TRUNCATIONS(prefix, pair, element, form, x, y, z, wide, features) \
    BUSTER_CHECK_X86_TRUNCATION(prefix pair "128", element, 16, X86_FORM_##form##_##x, 16, features) \
    BUSTER_CHECK_X86_TRUNCATION(prefix pair "256", element, 32, X86_FORM_##form##_##y, 16, features) \
    BUSTER_CHECK_X86_TRUNCATION(prefix pair "512", element, 64, X86_FORM_##form##_##z, wide, features) \
    BUSTER_CHECK_X86_TRUNCATION_STORES(prefix pair, element, form, x, y, z, features)

// A test's rule (FLAGS of vectors, KTEST of masks of `bytes` bytes): its form's flags, then 1 where `condition` holds
// (CHECK_X86_CC_*), and where `then` names a second condition (CHECK_X86_THEN_* with it), its value where that one does.
#define BUSTER_CHECK_X86_TEST(name, family, bytes, swap, form, condition, then, features) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, 1, bytes, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, then, false, features, 0, CHECK_X86_ROUND_NONE, CHECK_X86_CC_##condition)
// The SSE compares into an int of a kind (comi, comisd, ucomi or ucomisd): equal and not, of unordered operands 0 and 1
// by the parity; less and less or equal, by b's greater; greater.
#define BUSTER_CHECK_X86_COMPARISONS(kind, form) \
    BUSTER_CHECK_X86_TEST(kind "eq", FLAGS, 16, false, form, E, CHECK_X86_THEN_ZERO | CHECK_X86_CC_P, 0) \
    BUSTER_CHECK_X86_TEST(kind "lt", FLAGS, 16, true, form, A, 0, 0) \
    BUSTER_CHECK_X86_TEST(kind "le", FLAGS, 16, true, form, AE, 0, 0) \
    BUSTER_CHECK_X86_TEST(kind "gt", FLAGS, 16, false, form, A, 0, 0) \
    BUSTER_CHECK_X86_TEST(kind "ge", FLAGS, 16, false, form, AE, 0, 0) \
    BUSTER_CHECK_X86_TEST(kind "neq", FLAGS, 16, false, form, NE, CHECK_X86_THEN_ONE | CHECK_X86_CC_P, 0)
// A SYSTEM rule: its integers' bytes, its form and its layout (CHECK_X86_LAYOUT_*); and a VECTOR_MEMORY one, its
// elements' and vector's bytes.
#define BUSTER_CHECK_X86_SYSTEM(name, bytes, form, layout, features) \
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_" name, CHECK_X86_LOWER_SYSTEM, bytes, bytes, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, \
                                CHECK_X86_LAYOUT_##layout, features)
#define BUSTER_CHECK_X86_MEMORY(name, element, bytes, form, layout, features) \
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_" name, CHECK_X86_LOWER_VECTOR_MEMORY, element, bytes, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, \
                                CHECK_X86_LAYOUT_##layout, features)
// A MASK_TERNARY rule: its form and the one its rounding control picks, and where its result goes (CHECK_X86_INTO_*).
#define BUSTER_CHECK_X86_FMA(name, element, bytes, form, round_form, into) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_MASK_TERNARY, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, 0, false, 0, X86_FORM_##round_form, CHECK_X86_ROUND_ER, CHECK_X86_INTO_##into)
// A REDUCE rule of 64 bytes' elements.
#define BUSTER_CHECK_X86_REDUCE(name, element, reduction) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_reduce_" name, sizeof("__builtin_ia32_reduce_" name) - 1, CHECK_X86_LOWER_REDUCE, element, 64, false, false, \
      BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_NONE, 0, false, 0, 0, CHECK_X86_ROUND_NONE, CHECK_X86_REDUCE_##reduction)
// A rule with both an immediate argument (its index) and a rounding control.
#define BUSTER_CHECK_X86_ROUNDED_IMMEDIATE(name, family, element, bytes, immediate, form, features, round_form, rounding) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, immediate, X86_FORM_##form, 0, false, \
      features, X86_FORM_##round_form, CHECK_X86_ROUND_##rounding, 0)
// AVX512-FP16's rules (its extension, X86_EXTENSION_AVX512FP16, which implies AVX-512BW, DQ and VL): one with another
// vector's bytes (`other`), one with a rounding control, an FMA (MASK_TERNARY's, as BUSTER_CHECK_X86_FMA), a complex one
// (MASK_MERGE's of 32-bit lanes, a complex number each, by its merge: CHECK_X86_MERGE_*) and the scalar compare.
#define BUSTER_CHECK_X86_FP16 BUSTER_X86_EXTENSION(AVX512FP16)
#define BUSTER_CHECK_X86_HALF(name, family, element, bytes, form, other) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, other, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_CHECK_X86_FP16)
#define BUSTER_CHECK_X86_HALF_ROUNDED(name, family, element, bytes, form, other, round_form, rounding) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, other, false, 0, X86_FORM_##round_form, CHECK_X86_ROUND_##rounding, 0, BUSTER_CHECK_X86_FP16)
#define BUSTER_CHECK_X86_HALF_FMA(name, element, bytes, form, round_form, into) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_MASK_TERNARY, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, 0, false, 0, X86_FORM_##round_form, CHECK_X86_ROUND_ER, CHECK_X86_INTO_##into, BUSTER_CHECK_X86_FP16)
// A REDUCE rule of half-precision elements, of 16, 32 or 64 bytes.
#define BUSTER_CHECK_X86_HALF_REDUCE(name, bytes, reduction) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_reduce_" name, sizeof("__builtin_ia32_reduce_" name) - 1, CHECK_X86_LOWER_REDUCE, 2, bytes, false, false, \
      BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_NONE, 0, false, 0, 0, CHECK_X86_ROUND_NONE, CHECK_X86_REDUCE_##reduction, BUSTER_CHECK_X86_FP16)
#define BUSTER_CHECK_X86_HALF_COMPLEX(name, bytes, form, round_form, rounding, merge) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_MASK_MERGE, 4, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, 0, false, 0, X86_FORM_##round_form, CHECK_X86_ROUND_##rounding, CHECK_X86_MERGE_##merge, BUSTER_CHECK_X86_FP16)
// The conversions of half-precision elements to an integer type and back at each width (`x`, `y` and `z` the forms'
// operand suffixes, the 512-bit one with its rounding control's `zr`), each of a's bytes per lane and w's bytes.
#define BUSTER_CHECK_X86_HALF_CONVERSIONS(name, form, e1, b1, o1, x, e2, b2, o2, y, e3, b3, o3, z, zr, rounding) \
    BUSTER_CHECK_X86_HALF(name "128_mask", MASK_UNARY, e1, b1, form##_##x, o1) \
    BUSTER_CHECK_X86_HALF(name "256_mask", MASK_UNARY, e2, b2, form##_##y, o2) \
    BUSTER_CHECK_X86_HALF_ROUNDED(name "512_mask", MASK_UNARY, e3, b3, form##_##z, o3, form##_##zr, rounding)
// XOP's compares (XOP_COMPARE's, its extension's) of 16 bytes' elements, signed or not, with their features past AVX-512F
// and VL: AVX-512BW's VPCMPB, VPCMPW, VPMOVM2B and VPMOVM2W, AVX-512DQ's VPMOVM2D and VPMOVM2Q.
#define BUSTER_CHECK_X86_XOP_COMPARE(name, element, is_signed, features) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_XOP_COMPARE, element, 16, is_signed, false, 2, X86_FORM_NONE, 0, false, \
      features, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(XOP))
// XOP's instructions of its own (its extension's, no feature past AVX-512F and VL), as clang's code has them for every
// target: `family`'s row of the form.
#define BUSTER_CHECK_X86_XOP(name, family, element, bytes, swap, form) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, 0, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(XOP))
// AVX10.2's masked load and store of bf16's first element, the whole vector's under the mask, as AVX512-FP16's of a
// half's are.
#define BUSTER_CHECK_X86_BF16_MEMORY(name, family) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, 2, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_NONE, 0, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(AVX10_2))
// AVX-NE-CONVERT's loads (its extension's), VECTOR_MEMORY's LOAD: a bf16 or a half broadcast into every float element,
// or a vector of them's even or odd elements converted, from memory at p.
#define BUSTER_CHECK_X86_NE_CONVERT(name, bytes, form) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_VECTOR_MEMORY, 4, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, CHECK_X86_LAYOUT_LOAD, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(AVXNECONVERT))
// AVX-VNNI-INT8's dot products of bytes and AVX-VNNI-INT16's of words, TERNARY's into the accumulator, at 16, 32 and 64
// bytes in AVX10.2's EVEX forms, as clang's code of AVX10.2's intrinsics has them (the masked ones of 16 and 32 bytes, and
// those of 64 bytes); the headers' unmasked ones of 16 and 32 bytes are macros of the builtins.
#define BUSTER_CHECK_X86_VNNI_INT(name, form) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name "128", sizeof("__builtin_ia32_" name "128") - 1, CHECK_X86_LOWER_TERNARY, 4, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form##_X_X_XM128_EVEX, 0, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(AVX10_2) }, \
    { "__builtin_ia32_" name "256", sizeof("__builtin_ia32_" name "256") - 1, CHECK_X86_LOWER_TERNARY, 4, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form##_Y_Y_YM256_EVEX, 0, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(AVX10_2) }, \
    { "__builtin_ia32_" name "512", sizeof("__builtin_ia32_" name "512") - 1, CHECK_X86_LOWER_TERNARY, 4, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form##_Z_Z_ZM512, 0, false, 0, 0, CHECK_X86_ROUND_NONE, 0, BUSTER_X86_EXTENSION(AVX10_2))
// A rule of an extension's (`extensions`, X86Extension's bits), of `family`'s row of the form, with its last vector of
// `other` bytes where they aren't the others'.
#define BUSTER_CHECK_X86_EXTENDED(name, family, element, bytes, form, other, extensions) \
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, other, false, 0, 0, CHECK_X86_ROUND_NONE, 0, extensions)

    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_cmp", "b", "w", "d", "q", "_mask", CHECK_X86_LOWER_COMPARE, true, 2)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_ucmp", "b", "w", "d", "q", "_mask", CHECK_X86_LOWER_COMPARE, false, 2)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_select", "b_", "w_", "d_", "q_", "", CHECK_X86_LOWER_SELECT, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    // The floating selects are the integer ones of their elements' bytes: a select moves bits.
    BUSTER_CHECK_X86_WIDTHS("__builtin_ia32_selectps_", "", CHECK_X86_LOWER_SELECT, 4, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_WIDTHS("__builtin_ia32_selectpd_", "", CHECK_X86_LOWER_SELECT, 8, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_WIDTHS("__builtin_ia32_selectph_", "", CHECK_X86_LOWER_SELECT, 2, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_WIDTHS("__builtin_ia32_selectpbf_", "", CHECK_X86_LOWER_SELECT, 2, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_selectss_128", CHECK_X86_LOWER_SELECT_SCALAR, 4, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVSS_X_X_X_EVEX, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_selectsd_128", CHECK_X86_LOWER_SELECT_SCALAR, 8, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVSD_X_X_X_EVEX, 0)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_loaddqu", "qi", "hi", "si", "di", "_mask", CHECK_X86_LOWER_LOAD, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_storedqu", "qi", "hi", "si", "di", "_mask", CHECK_X86_LOWER_STORE, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    // The masked loads and stores of their own moves: floating elements', aligned ones' (which fault on an unaligned
    // address, as clang's do), a scalar's (the builtin's is the whole vector's under the mask's low bits, as clang's), and
    // the expanding loads (the elements under the mask from consecutive ones in memory).
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_loadups", "_mask", CHECK_X86_LOWER_LOAD, 4, false, VMOVUPS, X_XM128_EVEX, Y_YM256_EVEX, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_loadupd", "_mask", CHECK_X86_LOWER_LOAD, 8, false, VMOVUPD, X_XM128_EVEX, Y_YM256_EVEX, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_loadaps", "_mask", CHECK_X86_LOWER_LOAD, 4, false, VMOVAPS, X_XM128_EVEX, Y_YM256_EVEX, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_loadapd", "_mask", CHECK_X86_LOWER_LOAD, 8, false, VMOVAPD, X_XM128_EVEX, Y_YM256_EVEX, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_movdqa32load", "_mask", CHECK_X86_LOWER_LOAD, 4, false, VMOVDQA32, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_movdqa64load", "_mask", CHECK_X86_LOWER_LOAD, 8, false, VMOVDQA64, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_storeups", "_mask", CHECK_X86_LOWER_STORE, 4, false, VMOVUPS, XM128_X_EVEX, YM256_Y_EVEX, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_storeupd", "_mask", CHECK_X86_LOWER_STORE, 8, false, VMOVUPD, XM128_X_EVEX, YM256_Y_EVEX, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_storeaps", "_mask", CHECK_X86_LOWER_STORE, 4, false, VMOVAPS, XM128_X_EVEX, YM256_Y_EVEX, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_storeapd", "_mask", CHECK_X86_LOWER_STORE, 8, false, VMOVAPD, XM128_X_EVEX, YM256_Y_EVEX, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_movdqa32store", "_mask", CHECK_X86_LOWER_STORE, 4, false, VMOVDQA32, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_movdqa64store", "_mask", CHECK_X86_LOWER_STORE, 8, false, VMOVDQA64, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_loadss128_mask", CHECK_X86_LOWER_LOAD, 4, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVUPS_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_loadsd128_mask", CHECK_X86_LOWER_LOAD, 8, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVUPD_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_storess128_mask", CHECK_X86_LOWER_STORE, 4, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVUPS_XM128_X_EVEX, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_storesd128_mask", CHECK_X86_LOWER_STORE, 8, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVUPD_XM128_X_EVEX, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloadqi", "_mask", CHECK_X86_LOWER_LOAD, 1, false, VPEXPANDB, X_XM128, Y_YM256, Z_ZM512, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloadhi", "_mask", CHECK_X86_LOWER_LOAD, 2, false, VPEXPANDW, X_XM128, Y_YM256, Z_ZM512, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloadsi", "_mask", CHECK_X86_LOWER_LOAD, 4, false, VPEXPANDD, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloaddi", "_mask", CHECK_X86_LOWER_LOAD, 8, false, VPEXPANDQ, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloadsf", "_mask", CHECK_X86_LOWER_LOAD, 4, false, VEXPANDPS, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandloaddf", "_mask", CHECK_X86_LOWER_LOAD, 8, false, VEXPANDPD, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_kmovb", CHECK_X86_LOWER_KMOV, 1, 1, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_kmovw", CHECK_X86_LOWER_KMOV, 1, 2, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_kmovd", CHECK_X86_LOWER_KMOV, 1, 4, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_kmovq", CHECK_X86_LOWER_KMOV, 1, 8, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pshufb", VPSHUFB, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmaddwd", VPMADDWD, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmaddubsw", VPMADDUBSW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_packusdw", VPACKUSDW, false, BUSTER_CHECK_X86_BW)
    // VPERMB's indices come first (vvvv), the permuted data second (rm): the builtin's (data, indices).
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_permvarqi", CHECK_X86_LOWER_BINARY, 1, true, VPERMB, X_X_XM128, Y_Y_YM256, Z_Z_ZM512,
                           BUSTER_CHECK_X86_BW | BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_aesenc128", CHECK_X86_LOWER_BINARY, 1, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE,
                          X86_FORM_VAESENC_X_X_XM128_EVEX, BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_aesenclast128", CHECK_X86_LOWER_BINARY, 1, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE,
                          X86_FORM_VAESENCLAST_X_X_XM128_EVEX, BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psllwi", VPSLLW, 2, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psrlwi", VPSRLW, 2, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psrawi", VPSRAW, 2, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_pslldi", VPSLLD, 4, 0)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psrldi", VPSRLD, 4, 0)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psradi", VPSRAD, 4, 0)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psllqi", VPSLLQ, 8, 0)
    BUSTER_CHECK_X86_SHIFTS("__builtin_ia32_psrlqi", VPSRLQ, 8, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_ext_v16qi", CHECK_X86_LOWER_EXTRACT, 1, 16, false, false, 1, X86_FORM_VPEXTRB_RM32_X_IMM8_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_ext_v8hi", CHECK_X86_LOWER_EXTRACT, 2, 16, false, false, 1, X86_FORM_VPEXTRW_R32_X_IMM8_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_ext_v4si", CHECK_X86_LOWER_EXTRACT, 4, 16, false, false, 1, X86_FORM_VPEXTRD_RM32_X_IMM8_EVEX, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_ext_v2di", CHECK_X86_LOWER_EXTRACT, 8, 16, false, false, 1, X86_FORM_VPEXTRQ_RM64_X_IMM8_EVEX, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_set_v16qi", CHECK_X86_LOWER_INSERT, 1, 16, false, false, 2, X86_FORM_VPINSRB_X_X_RM32_IMM8_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_set_v8hi", CHECK_X86_LOWER_INSERT, 2, 16, false, false, 2, X86_FORM_VPINSRW_X_X_RM32_IMM8_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_set_v4si", CHECK_X86_LOWER_INSERT, 4, 16, false, false, 2, X86_FORM_VPINSRD_X_X_RM32_IMM8_EVEX, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_set_v2di", CHECK_X86_LOWER_INSERT, 8, 16, false, false, 2, X86_FORM_VPINSRQ_X_X_RM64_IMM8_EVEX, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pmovmskb128", CHECK_X86_LOWER_MOVEMASK, 1, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVMSKB_R32_X, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pdep_si", CHECK_X86_LOWER_GENERAL, 4, 4, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_PDEP_R32_R32_RM32, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pdep_di", CHECK_X86_LOWER_GENERAL, 8, 8, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_PDEP_R64_R64_RM64, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pext_si", CHECK_X86_LOWER_GENERAL, 4, 4, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_PEXT_R32_R32_RM32, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pext_di", CHECK_X86_LOWER_GENERAL, 8, 8, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_PEXT_R64_R64_RM64, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_bzhi_si", CHECK_X86_LOWER_GENERAL, 4, 4, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_BZHI_R32_RM32_R32, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_bzhi_di", CHECK_X86_LOWER_GENERAL, 8, 8, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_BZHI_R64_RM64_R64, BUSTER_X86_FEATURE(BMI2))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_undef128", CHECK_X86_LOWER_UNDEFINED, 1, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_undef256", CHECK_X86_LOWER_UNDEFINED, 1, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_undef512", CHECK_X86_LOWER_UNDEFINED, 1, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, 0, 0)
    // The opmask operations: their masks' bits from their integers and back through KMOV, as clang -O0's code does.
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KANDW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kaddhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KADDW_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kunpckhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KUNPCKBW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kunpcksi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KUNPCKWD_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kunpckdi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KUNPCKDQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kordi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KORQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxordi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KXORQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_ktestzdi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KANDQ_K_K_K, true, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kshiftlihi", CHECK_X86_LOWER_KSHIFT, 2, X86_FORM_KSHIFTLW_K_K_IMM8, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kshiftlidi", CHECK_X86_LOWER_KSHIFT, 8, X86_FORM_KSHIFTLQ_K_K_IMM8, false, BUSTER_CHECK_X86_BW)
    // Compress and expand, of bytes and words with AVX-512VBMI2.
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressqi", "_mask", CHECK_X86_LOWER_COMPRESS, 1, false, VPCOMPRESSB, XM128_X, YM256_Y, ZM512_Z, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compresshi", "_mask", CHECK_X86_LOWER_COMPRESS, 2, false, VPCOMPRESSW, XM128_X, YM256_Y, ZM512_Z, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compresssi", "_mask", CHECK_X86_LOWER_COMPRESS, 4, false, VPCOMPRESSD, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressdi", "_mask", CHECK_X86_LOWER_COMPRESS, 8, false, VPCOMPRESSQ, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandqi", "_mask", CHECK_X86_LOWER_COMPRESS, 1, true, VPEXPANDB, X_XM128, Y_YM256, Z_ZM512, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandhi", "_mask", CHECK_X86_LOWER_COMPRESS, 2, true, VPEXPANDW, X_XM128, Y_YM256, Z_ZM512, BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandsi", "_mask", CHECK_X86_LOWER_COMPRESS, 4, true, VPEXPANDD, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expanddi", "_mask", CHECK_X86_LOWER_COMPRESS, 8, true, VPEXPANDQ, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstoreqi", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 1, false, VPCOMPRESSB, XM128_X, YM256_Y, ZM512_Z,
                              BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstorehi", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 2, false, VPCOMPRESSW, XM128_X, YM256_Y, ZM512_Z,
                              BUSTER_X86_FEATURE(AVX512VBMI2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstoresi", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 4, false, VPCOMPRESSD, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstoredi", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 8, false, VPCOMPRESSQ, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compresssf", "_mask", CHECK_X86_LOWER_COMPRESS, 4, false, VCOMPRESSPS, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressdf", "_mask", CHECK_X86_LOWER_COMPRESS, 8, false, VCOMPRESSPD, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expandsf", "_mask", CHECK_X86_LOWER_COMPRESS, 4, true, VEXPANDPS, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_expanddf", "_mask", CHECK_X86_LOWER_COMPRESS, 8, true, VEXPANDPD, X_XM128, Y_YM256, Z_ZM512, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstoresf", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 4, false, VCOMPRESSPS, XM128_X, YM256_Y, ZM512_Z, 0)
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_compressstoredf", "_mask", CHECK_X86_LOWER_COMPRESS_STORE, 8, false, VCOMPRESSPD, XM128_X, YM256_Y, ZM512_Z, 0)
    // Masks of elements' top bits, and vectors of masks' bits.
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtb2mask", CHECK_X86_LOWER_VECTOR_MASK, 1, false, VPMOVB2M, K_X, K_Y, K_Z, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtw2mask", CHECK_X86_LOWER_VECTOR_MASK, 2, false, VPMOVW2M, K_X, K_Y, K_Z, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtd2mask", CHECK_X86_LOWER_VECTOR_MASK, 4, false, VPMOVD2M, K_X, K_Y, K_Z, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtq2mask", CHECK_X86_LOWER_VECTOR_MASK, 8, false, VPMOVQ2M, K_X, K_Y, K_Z, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtmask2b", CHECK_X86_LOWER_MASK_VECTOR, 1, false, VPMOVM2B, X_K, Y_K, Z_K, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtmask2w", CHECK_X86_LOWER_MASK_VECTOR, 2, false, VPMOVM2W, X_K, Y_K, Z_K, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtmask2d", CHECK_X86_LOWER_MASK_VECTOR, 4, false, VPMOVM2D, X_K, Y_K, Z_K, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtmask2q", CHECK_X86_LOWER_MASK_VECTOR, 8, false, VPMOVM2Q, X_K, Y_K, Z_K, BUSTER_X86_FEATURE(AVX512DQ))
    // Truncating conversions of a vector's low element: `other` is the integer's bytes.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_cvttsd2si", CHECK_X86_LOWER_CONVERT, 8, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VCVTTSD2SI_R32_XM64_EVEX, 4, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_cvttsd2si64", CHECK_X86_LOWER_CONVERT, 8, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VCVTTSD2SI_R64_XM64_EVEX, 8, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_cvttss2si", CHECK_X86_LOWER_CONVERT, 4, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VCVTTSS2SI_R32_XM32_EVEX, 4, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_cvttss2si64", CHECK_X86_LOWER_CONVERT, 4, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VCVTTSS2SI_R64_XM32_EVEX, 8, 0)
    // Lanes: extracted under a mask, inserted, aligned and shuffled, by an immediate.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_extracti32x4_mask", CHECK_X86_LOWER_EXTRACT_LANE, 4, 64, false, 1, X86_FORM_VEXTRACTI32X4_XM128_Z_IMM8, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_extracti64x4_mask", CHECK_X86_LOWER_EXTRACT_LANE, 8, 64, false, 1, X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_inserti32x4", CHECK_X86_LOWER_BINARY_IMMEDIATE, 4, 64, false, 2, X86_FORM_VINSERTI32X4_Z_Z_XM128_IMM8, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_insert128i256", CHECK_X86_LOWER_BINARY_IMMEDIATE, 4, 32, false, 2, X86_FORM_VINSERTI32X4_Y_Y_XM128_IMM8, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_inserti64x4", CHECK_X86_LOWER_BINARY_IMMEDIATE, 8, 64, false, 2, X86_FORM_VINSERTI64X4_Z_Z_YM256_IMM8, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_alignd512", CHECK_X86_LOWER_BINARY_IMMEDIATE, 4, 64, false, 2, X86_FORM_VALIGND_Z_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_alignq512", CHECK_X86_LOWER_BINARY_IMMEDIATE, 8, 64, false, 2, X86_FORM_VALIGNQ_Z_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_shuf_i32x4", CHECK_X86_LOWER_BINARY_IMMEDIATE, 4, 64, false, 2, X86_FORM_VSHUFI32X4_Z_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_shuf_i64x2", CHECK_X86_LOWER_BINARY_IMMEDIATE, 8, 64, false, 2, X86_FORM_VSHUFI64X2_Z_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vgf2p8affineqb_v64qi", CHECK_X86_LOWER_BINARY_IMMEDIATE, 1, 64, false, 2, X86_FORM_VGF2P8AFFINEQB_Z_Z_ZM512_IMM8, 0,
                                BUSTER_X86_FEATURE(GFNI))
    // One vector by an immediate: VPSHUFD's destination in reg, VPROLD's and VPSLLDQ's in vvvv (`swap`).
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pshufd512", CHECK_X86_LOWER_UNARY_IMMEDIATE, 4, 64, false, 1, X86_FORM_VPSHUFD_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_prold512", CHECK_X86_LOWER_UNARY_IMMEDIATE, 4, 64, true, 1, X86_FORM_VPROLD_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pslldqi128_byteshift", CHECK_X86_LOWER_UNARY_IMMEDIATE, 1, 16, true, 1, X86_FORM_VPSLLDQ_X_XM128_IMM8, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpconflictsi_512", CHECK_X86_LOWER_UNARY, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPCONFLICTD_Z_ZM512, 0,
                                BUSTER_X86_FEATURE(AVX512CD))
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpconflictdi_512", CHECK_X86_LOWER_UNARY, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPCONFLICTQ_Z_ZM512, 0,
                                BUSTER_X86_FEATURE(AVX512CD))
    // Three vectors: the first is the destination. VPTERNLOG's immediate is its fourth argument and its mask its fifth.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pternlogd512_mask", CHECK_X86_LOWER_TERNARY, 4, 64, false, 3, X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pternlogq512_mask", CHECK_X86_LOWER_TERNARY, 8, 64, false, 3, X86_FORM_VPTERNLOGQ_Z_Z_ZM512_IMM8, 0, 0)
    // The dot products of AVX-512VNNI: of bytes (unsigned by signed) and of words, each with signed saturation (`s`).
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpdpbusd", CHECK_X86_LOWER_TERNARY, 4, false, VPDPBUSD, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512VNNI))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpdpbusds", CHECK_X86_LOWER_TERNARY, 4, false, VPDPBUSDS, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512VNNI))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpdpwssd", CHECK_X86_LOWER_TERNARY, 4, false, VPDPWSSD, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512VNNI))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpdpwssds", CHECK_X86_LOWER_TERNARY, 4, false, VPDPWSSDS, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512VNNI))
    // VPERMT2's first table is its destination, its indices in vvvv and its second table in rm: the builtin's (a, indices, b).
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2vard512", CHECK_X86_LOWER_TERNARY, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2D_Z_Z_ZM512, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2varq512", CHECK_X86_LOWER_TERNARY, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2Q_Z_Z_ZM512, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2varhi512", CHECK_X86_LOWER_TERNARY, 2, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2W_Z_Z_ZM512, 0,
                                BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2varqi512", CHECK_X86_LOWER_TERNARY, 1, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2B_Z_Z_ZM512, 0,
                                BUSTER_CHECK_X86_BW | BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2varqi256", CHECK_X86_LOWER_TERNARY, 1, 32, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2B_Y_Y_YM256, 0,
                                BUSTER_CHECK_X86_BW | BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpermi2varqi128", CHECK_X86_LOWER_TERNARY, 1, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMT2B_X_X_XM128, 0,
                                BUSTER_CHECK_X86_BW | BUSTER_X86_FEATURE(AVX512VBMI))
    // Truncations under a mask: `other` is the result's bytes, and its passthrough's; and the masked truncating stores
    // (`mem`), with the same forms into memory. Each of VPMOV's, VPMOVS's (signed saturation) and VPMOVUS's (unsigned)
    // of doublewords to bytes and words and quadwords to bytes, words and doublewords at 16, 32 and 64 bytes, and of
    // words to bytes with AVX-512BW, but for the builtins clang hasn't (pmovqd256_mask, pmovwb256_mask).
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmov", "db", 4, VPMOVDB, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmov", "dw", 4, VPMOVDW, XM64_X, XM128_Y, YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmov", "qb", 8, VPMOVQB, XM16_X, XM32_Y, XM64_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmov", "qw", 8, VPMOVQW, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATION("__builtin_ia32_pmovqd128", 8, 16, X86_FORM_VPMOVQD_XM64_X, 16, 0)
    BUSTER_CHECK_X86_TRUNCATION("__builtin_ia32_pmovqd512", 8, 64, X86_FORM_VPMOVQD_YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATION_STORES("__builtin_ia32_pmovqd", 8, VPMOVQD, XM64_X, XM128_Y, YM256_Z, 0)
    BUSTER_CHECK_X86_TRUNCATION("__builtin_ia32_pmovwb128", 2, 16, X86_FORM_VPMOVWB_XM64_X, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TRUNCATION("__builtin_ia32_pmovwb512", 2, 64, X86_FORM_VPMOVWB_YM256_Z, 32, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TRUNCATION_STORES("__builtin_ia32_pmovwb", 2, VPMOVWB, XM64_X, XM128_Y, YM256_Z, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "db", 4, VPMOVSDB, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "dw", 4, VPMOVSDW, XM64_X, XM128_Y, YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "qb", 8, VPMOVSQB, XM16_X, XM32_Y, XM64_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "qw", 8, VPMOVSQW, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "qd", 8, VPMOVSQD, XM64_X, XM128_Y, YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovs", "wb", 2, VPMOVSWB, XM64_X, XM128_Y, YM256_Z, 32, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "db", 4, VPMOVUSDB, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "dw", 4, VPMOVUSDW, XM64_X, XM128_Y, YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "qb", 8, VPMOVUSQB, XM16_X, XM32_Y, XM64_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "qw", 8, VPMOVUSQW, XM32_X, XM64_Y, XM128_Z, 16, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "qd", 8, VPMOVUSQD, XM64_X, XM128_Y, YM256_Z, 32, 0)
    BUSTER_CHECK_X86_TRUNCATIONS("__builtin_ia32_pmovus", "wb", 2, VPMOVUSWB, XM64_X, XM128_Y, YM256_Z, 32, BUSTER_CHECK_X86_BW)
    // Gathers and scatters: `other` is the index's bytes, or a scatter's value's.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_gathersiv16si", CHECK_X86_LOWER_GATHER, 4, 64, false, 4, X86_FORM_VPGATHERDD_Z_VM32Z, 64, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_gathersiv8di", CHECK_X86_LOWER_GATHER, 8, 64, false, 4, X86_FORM_VPGATHERDQ_Z_VM32Y, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_gatherdiv8di", CHECK_X86_LOWER_GATHER, 8, 64, false, 4, X86_FORM_VPGATHERQQ_Z_VM64Z, 64, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_scatterdiv16si", CHECK_X86_LOWER_SCATTER, 8, 64, false, 4, X86_FORM_VPSCATTERQD_VM64Z_Y, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_scatterdiv8di", CHECK_X86_LOWER_SCATTER, 8, 64, false, 4, X86_FORM_VPSCATTERQQ_VM64Z_Z, 64, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_scattersiv16si", CHECK_X86_LOWER_SCATTER, 4, 64, false, 4, X86_FORM_VPSCATTERDD_VM32Z_Z, 64, 0)
    // Byte blends by a vector's bytes' top bits (VEX), and VPSHUFBITQMB's masks.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pblendvb128", CHECK_X86_LOWER_BLENDV, 1, 16, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPBLENDVB_X_X_XM128_X, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pblendvb256", CHECK_X86_LOWER_BLENDV, 1, 32, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPBLENDVB_Y_Y_YM256_Y, 0,
                                BUSTER_X86_FEATURE(AVX2))
    BUSTER_CHECK_X86_SUFFIXED("__builtin_ia32_vpshufbitqmb", "_mask", CHECK_X86_LOWER_MASK_BINARY, 1, false, VPSHUFBITQMB, K_X_XM128, K_Y_YM256, K_Z_ZM512,
                              BUSTER_CHECK_X86_BW | BUSTER_X86_FEATURE(AVX512BITALG))
    // More of the existing families: permutes by a vector of indices (the builtin's data, then its indices: `swap`),
    // variable shifts, and two-vector operations.
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvardi512", CHECK_X86_LOWER_BINARY, 8, 64, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMQ_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvarsi512", CHECK_X86_LOWER_BINARY, 4, 64, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMD_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvarsi256", CHECK_X86_LOWER_BINARY, 4, 32, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMD_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvarhi512", CHECK_X86_LOWER_BINARY, 2, 64, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMW_Z_Z_ZM512,
                          BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvarhi256", CHECK_X86_LOWER_BINARY, 2, 32, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMW_Y_Y_YM256,
                          BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_permvarhi128", CHECK_X86_LOWER_BINARY, 2, 16, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPERMW_X_X_XM128,
                          BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psllv16si", CHECK_X86_LOWER_BINARY, 4, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSLLVD_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psllv8di", CHECK_X86_LOWER_BINARY, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSLLVQ_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psrlv16si", CHECK_X86_LOWER_BINARY, 4, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRLVD_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psrlv8di", CHECK_X86_LOWER_BINARY, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRLVQ_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psrav16si", CHECK_X86_LOWER_BINARY, 4, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRAVD_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psrav8di", CHECK_X86_LOWER_BINARY, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRAVQ_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pmuludq512", CHECK_X86_LOWER_BINARY, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMULUDQ_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psadbw512", CHECK_X86_LOWER_BINARY, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSADBW_Z_Z_ZM512,
                          BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vpmultishiftqb512", CHECK_X86_LOWER_BINARY, 1, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMULTISHIFTQB_Z_Z_ZM512,
                          BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_aesenc256", CHECK_X86_LOWER_BINARY, 1, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VAESENC_Y_Y_YM256_EVEX,
                          BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_aesenc512", CHECK_X86_LOWER_BINARY, 1, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VAESENC_Z_Z_ZM512,
                          BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_aesenclast512", CHECK_X86_LOWER_BINARY, 1, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VAESENCLAST_Z_Z_ZM512,
                          BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psraqi512", CHECK_X86_LOWER_SHIFT, 8, 64, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRAQ_Z_Z_XM128, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psraqi256", CHECK_X86_LOWER_SHIFT, 8, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRAQ_Y_Y_XM128, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_psraqi128", CHECK_X86_LOWER_SHIFT, 8, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPSRAQ_X_X_XM128, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_movmskps", CHECK_X86_LOWER_MOVEMASK, 4, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVMSKPS_R32_X, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_movmskps256", CHECK_X86_LOWER_MOVEMASK, 4, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVMSKPS_R32_Y, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_movmskpd", CHECK_X86_LOWER_MOVEMASK, 8, 16, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVMSKPD_R32_X, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_movmskpd256", CHECK_X86_LOWER_MOVEMASK, 8, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VMOVMSKPD_R32_Y, 0)
    BUSTER_CHECK_X86_RULE("__builtin_ia32_pmovmskb256", CHECK_X86_LOWER_MOVEMASK, 1, 32, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVMSKB_R32_Y,
                          BUSTER_X86_FEATURE(AVX2))
    // An element of 64 bits of a 256-bit vector: its lane extracted first where it is past the low one.
    BUSTER_CHECK_X86_RULE("__builtin_ia32_vec_ext_v4di", CHECK_X86_LOWER_EXTRACT, 8, 32, false, false, 1, X86_FORM_VPEXTRQ_RM64_X_IMM8_EVEX, BUSTER_X86_FEATURE(AVX512DQ))
    // #148's SSE shuffles, which stb_image, dr_libs and minimp3 reach through the SSE2, SSSE3 and SSE4.1 intrinsics, by
    // the existing families: shuffles, a byte shift and an align by an immediate (the instruction's whole byte, as clang
    // -O0's code takes it). The packs, multiplications, maximums, minimums and conversion those intrinsics reach too are
    // the families' below.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pshufd", CHECK_X86_LOWER_UNARY_IMMEDIATE, 4, 16, false, 1, X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pshufhw", CHECK_X86_LOWER_UNARY_IMMEDIATE, 2, 16, false, 1, X86_FORM_VPSHUFHW_X_XM128_IMM8_EVEX, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pshuflw", CHECK_X86_LOWER_UNARY_IMMEDIATE, 2, 16, false, 1, X86_FORM_VPSHUFLW_X_XM128_IMM8_EVEX, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_psrldqi128_byteshift", CHECK_X86_LOWER_UNARY_IMMEDIATE, 1, 16, true, 1, X86_FORM_VPSRLDQ_X_XM128_IMM8, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_shufps", CHECK_X86_LOWER_BINARY_IMMEDIATE, 4, 16, false, 2, X86_FORM_VSHUFPS_X_X_XM128_IMM8_EVEX, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_palignr128", CHECK_X86_LOWER_BINARY_IMMEDIATE, 1, 16, false, 2, X86_FORM_VPALIGNR_X_X_XM128_IMM8_EVEX, 0, BUSTER_CHECK_X86_BW)
    // Two vectors' operations, the form's dest, a, b (or with `swap` dest, b, a: VPERMPS's and VPERMPD's indices first).
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_packssdw", VPACKSSDW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_packsswb", VPACKSSWB, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_packuswb", VPACKUSWB, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pavgb", VPAVGB, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pavgw", VPAVGW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmuldq", VPMULDQ, false, 0)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmulhrsw", VPMULHRSW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmulhuw", VPMULHUW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_BINARIES("__builtin_ia32_pmulhw", VPMULHW, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("pmuludq128", BINARY, 4, 16, VPMULUDQ_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("pmuludq256", BINARY, 4, 32, VPMULUDQ_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psadbw128", BINARY, 1, 16, VPSADBW_X_X_XM128_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psadbw256", BINARY, 1, 32, VPSADBW_Y_Y_YM256_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("vpmultishiftqb128", BINARY, 1, 16, VPMULTISHIFTQB_X_X_XM128, BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_ONE("vpmultishiftqb256", BINARY, 1, 32, VPMULTISHIFTQB_Y_Y_YM256, BUSTER_X86_FEATURE(AVX512VBMI))
    BUSTER_CHECK_X86_ONE("psravq128", BINARY, 8, 16, VPSRAVQ_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("psravq256", BINARY, 8, 32, VPSRAVQ_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("psllv16hi", BINARY, 2, 32, VPSLLVW_Y_Y_YM256, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psllv32hi", BINARY, 2, 64, VPSLLVW_Z_Z_ZM512, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrav16hi", BINARY, 2, 32, VPSRAVW_Y_Y_YM256, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrav32hi", BINARY, 2, 64, VPSRAVW_Z_Z_ZM512, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrlv16hi", BINARY, 2, 32, VPSRLVW_Y_Y_YM256, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrlv32hi", BINARY, 2, 64, VPSRLVW_Z_Z_ZM512, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psllv4di", BINARY, 8, 32, VPSLLVQ_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psllv8si", BINARY, 4, 32, VPSLLVD_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrav8si", BINARY, 4, 32, VPSRAVD_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrlv4di", BINARY, 8, 32, VPSRLVQ_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrlv8si", BINARY, 4, 32, VPSRLVD_Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("vgf2p8mulb_v32qi", BINARY, 1, 32, VGF2P8MULB_Y_Y_YM256_EVEX, BUSTER_X86_FEATURE(GFNI))
    BUSTER_CHECK_X86_ONE("vgf2p8mulb_v64qi", BINARY, 1, 64, VGF2P8MULB_Z_Z_ZM512, BUSTER_X86_FEATURE(GFNI))
    BUSTER_CHECK_X86_ONE("psllv8hi", BINARY, 2, 16, VPSLLVW_X_X_XM128, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrav8hi", BINARY, 2, 16, VPSRAVW_X_X_XM128, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrlv8hi", BINARY, 2, 16, VPSRLVW_X_X_XM128, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psllv2di", BINARY, 8, 16, VPSLLVQ_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psllv4si", BINARY, 4, 16, VPSLLVD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrav4si", BINARY, 4, 16, VPSRAVD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrlv2di", BINARY, 8, 16, VPSRLVQ_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("psrlv4si", BINARY, 4, 16, VPSRLVD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("vgf2p8mulb_v16qi", BINARY, 1, 16, VGF2P8MULB_X_X_XM128_EVEX, BUSTER_X86_FEATURE(GFNI))
    BUSTER_CHECK_X86_ONE("aesdec128", BINARY, 1, 16, VAESDEC_X_X_XM128_EVEX, BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesdec256", BINARY, 1, 32, VAESDEC_Y_Y_YM256_EVEX, BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesdec512", BINARY, 1, 64, VAESDEC_Z_Z_ZM512, BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesdeclast128", BINARY, 1, 16, VAESDECLAST_X_X_XM128_EVEX, BUSTER_X86_FEATURE(AES) | BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesdeclast256", BINARY, 1, 32, VAESDECLAST_Y_Y_YM256_EVEX, BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesdeclast512", BINARY, 1, 64, VAESDECLAST_Z_Z_ZM512, BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_ONE("aesenclast256", BINARY, 1, 32, VAESENCLAST_Y_Y_YM256_EVEX, BUSTER_X86_FEATURE(VAES))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_cvtne2ps2bf16_", CHECK_X86_LOWER_BINARY, 4, false, VCVTNE2PS2BF16, X_X_XM128, Y_Y_YM256, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ONE_OTHER("permvardf256", BINARY, 8, 32, true, VPERMPD_Y_Y_YM256, 0, 0)
    BUSTER_CHECK_X86_ONE_OTHER("permvardf512", BINARY, 8, 64, true, VPERMPD_Z_Z_ZM512, 0, 0)
    BUSTER_CHECK_X86_ONE_OTHER("permvarsf256", BINARY, 4, 32, true, VPERMPS_Y_Y_YM256_EVEX, 0, 0)
    BUSTER_CHECK_X86_ONE_OTHER("permvarsf512", BINARY, 4, 64, true, VPERMPS_Z_Z_ZM512, 0, 0)
    BUSTER_CHECK_X86_ONE_OTHER("permvardi256", BINARY, 8, 32, true, VPERMQ_Y_Y_YM256, 0, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_vpermilvarpd", CHECK_X86_LOWER_BINARY, 8, false, VPERMILPD, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("vpermilvarpd512", BINARY, 8, 64, VPERMILPD_Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_vpermilvarps", CHECK_X86_LOWER_BINARY, 4, false, VPERMILPS, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("vpermilvarps512", BINARY, 4, 64, VPERMILPS_Z_Z_ZM512, 0)
    // The SSE builtins of two vectors: 16 bytes without a width in the name and 32 with "256", and the scalar ones, whose
    // other elements are a's.
    BUSTER_CHECK_X86_SSE("__builtin_ia32_addsubpd", CHECK_X86_LOWER_BINARY, 8, false, VADDSUBPD, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_addsubps", CHECK_X86_LOWER_BINARY, 4, false, VADDSUBPS, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_haddpd", CHECK_X86_LOWER_BINARY, 8, false, VHADDPD, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_haddps", CHECK_X86_LOWER_BINARY, 4, false, VHADDPS, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_hsubpd", CHECK_X86_LOWER_BINARY, 8, false, VHSUBPD, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_hsubps", CHECK_X86_LOWER_BINARY, 4, false, VHSUBPS, X_X_XM128, Y_Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_maxpd", CHECK_X86_LOWER_BINARY, 8, false, VMAXPD, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_maxps", CHECK_X86_LOWER_BINARY, 4, false, VMAXPS, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_minpd", CHECK_X86_LOWER_BINARY, 8, false, VMINPD, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_minps", CHECK_X86_LOWER_BINARY, 4, false, VMINPS, X_X_XM128_EVEX, Y_Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_ONE("phaddd128", BINARY, 4, 16, VPHADDD_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phaddd256", BINARY, 4, 32, VPHADDD_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("phaddw128", BINARY, 2, 16, VPHADDW_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phaddw256", BINARY, 2, 32, VPHADDW_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("phaddsw128", BINARY, 2, 16, VPHADDSW_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phaddsw256", BINARY, 2, 32, VPHADDSW_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("phsubd128", BINARY, 4, 16, VPHSUBD_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phsubd256", BINARY, 4, 32, VPHSUBD_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("phsubw128", BINARY, 2, 16, VPHSUBW_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phsubw256", BINARY, 2, 32, VPHSUBW_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("phsubsw128", BINARY, 2, 16, VPHSUBSW_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("phsubsw256", BINARY, 2, 32, VPHSUBSW_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("psignb128", BINARY, 1, 16, VPSIGNB_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("psignb256", BINARY, 1, 32, VPSIGNB_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("psignw128", BINARY, 2, 16, VPSIGNW_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("psignw256", BINARY, 2, 32, VPSIGNW_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("psignd128", BINARY, 4, 16, VPSIGND_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("psignd256", BINARY, 4, 32, VPSIGND_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("maxsd", BINARY, 8, 16, VMAXSD_X_X_XM64_EVEX, 0)
    BUSTER_CHECK_X86_ONE("maxss", BINARY, 4, 16, VMAXSS_X_X_XM32_EVEX, 0)
    BUSTER_CHECK_X86_ONE("minsd", BINARY, 8, 16, VMINSD_X_X_XM64_EVEX, 0)
    BUSTER_CHECK_X86_ONE("minss", BINARY, 4, 16, VMINSS_X_X_XM32_EVEX, 0)
    BUSTER_CHECK_X86_ONE("cvtsd2ss", BINARY, 8, 16, VCVTSD2SS_X_X_XM64_EVEX, 0)
    // Shifts of each width by the count in an XMM register's low quadword (`other`'s 16 bytes past the first width).
    BUSTER_CHECK_X86_ONE("psllw128", BINARY, 2, 16, VPSLLW_X_X_XM128_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psllw256", BINARY, 2, 32, false, VPSLLW_Y_Y_XM128_EVEX, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psllw512", BINARY, 2, 64, false, VPSLLW_Z_Z_XM128, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("pslld128", BINARY, 4, 16, VPSLLD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("pslld256", BINARY, 4, 32, false, VPSLLD_Y_Y_XM128_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("pslld512", BINARY, 4, 64, false, VPSLLD_Z_Z_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE("psllq128", BINARY, 8, 16, VPSLLQ_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psllq256", BINARY, 8, 32, false, VPSLLQ_Y_Y_XM128_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psllq512", BINARY, 8, 64, false, VPSLLQ_Z_Z_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE("psraw128", BINARY, 2, 16, VPSRAW_X_X_XM128_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psraw256", BINARY, 2, 32, false, VPSRAW_Y_Y_XM128_EVEX, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psraw512", BINARY, 2, 64, false, VPSRAW_Z_Z_XM128, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrad128", BINARY, 4, 16, VPSRAD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrad256", BINARY, 4, 32, false, VPSRAD_Y_Y_XM128_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrad512", BINARY, 4, 64, false, VPSRAD_Z_Z_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE("psraq128", BINARY, 8, 16, VPSRAQ_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psraq256", BINARY, 8, 32, false, VPSRAQ_Y_Y_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psraq512", BINARY, 8, 64, false, VPSRAQ_Z_Z_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE("psrlw128", BINARY, 2, 16, VPSRLW_X_X_XM128_EVEX, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psrlw256", BINARY, 2, 32, false, VPSRLW_Y_Y_XM128_EVEX, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE_OTHER("psrlw512", BINARY, 2, 64, false, VPSRLW_Z_Z_XM128, 16, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("psrld128", BINARY, 4, 16, VPSRLD_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrld256", BINARY, 4, 32, false, VPSRLD_Y_Y_XM128_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrld512", BINARY, 4, 64, false, VPSRLD_Z_Z_XM128, 16, 0)
    BUSTER_CHECK_X86_ONE("psrlq128", BINARY, 8, 16, VPSRLQ_X_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrlq256", BINARY, 8, 32, false, VPSRLQ_Y_Y_XM128_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE_OTHER("psrlq512", BINARY, 8, 64, false, VPSRLQ_Z_Z_XM128, 16, 0)
    // One vector's operations, a narrowing conversion's result of `other` bytes; and the scalar ones of a's first element.
    BUSTER_CHECK_X86_ONE("cvtpd2dq", UNARY, 8, 16, VCVTPD2DQ_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtpd2dq256", UNARY, 8, 32, false, VCVTPD2DQ_X_YM256_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE("cvtpd2ps", UNARY, 8, 16, VCVTPD2PS_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtpd2ps256", UNARY, 8, 32, false, VCVTPD2PS_X_YM256_EVEX, 16, 0)
    BUSTER_CHECK_X86_ONE("cvttpd2dq", UNARY, 8, 16, VCVTTPD2DQ_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvttpd2dq256", UNARY, 8, 32, false, VCVTTPD2DQ_X_YM256_EVEX, 16, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_cvtps2dq", CHECK_X86_LOWER_UNARY, 4, false, VCVTPS2DQ, X_XM128_EVEX, Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_cvttps2dq", CHECK_X86_LOWER_UNARY, 4, false, VCVTTPS2DQ, X_XM128_EVEX, Y_YM256_EVEX, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_rcpps", CHECK_X86_LOWER_UNARY, 4, false, VRCPPS, X_XM128, Y_YM256, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_rsqrtps", CHECK_X86_LOWER_UNARY, 4, false, VRSQRTPS, X_XM128, Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("aesimc128", UNARY, 1, 16, VAESIMC_X_XM128, BUSTER_X86_FEATURE(AES))
    BUSTER_CHECK_X86_ONE("phminposuw128", UNARY, 2, 16, VPHMINPOSUW_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("vpconflictdi_128", UNARY, 8, 16, VPCONFLICTQ_X_XM128, BUSTER_X86_FEATURE(AVX512CD))
    BUSTER_CHECK_X86_ONE("vpconflictdi_256", UNARY, 8, 32, VPCONFLICTQ_Y_YM256, BUSTER_X86_FEATURE(AVX512CD))
    BUSTER_CHECK_X86_ONE("vpconflictsi_128", UNARY, 4, 16, VPCONFLICTD_X_XM128, BUSTER_X86_FEATURE(AVX512CD))
    BUSTER_CHECK_X86_ONE("vpconflictsi_256", UNARY, 4, 32, VPCONFLICTD_Y_YM256, BUSTER_X86_FEATURE(AVX512CD))
    BUSTER_CHECK_X86_ONE("vcvtneps2bf16128", UNARY, 4, 16, VCVTNEPS2BF16_X_XM128_EVEX, BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ONE_OTHER("vcvtneps2bf16256", UNARY, 4, 32, false, VCVTNEPS2BF16_X_YM256_EVEX, 16, BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ONE("rcpss", UNARY_SCALAR, 4, 16, VRCPSS_X_X_XM32, 0)
    BUSTER_CHECK_X86_ONE("rsqrtss", UNARY_SCALAR, 4, 16, VRSQRTSS_X_X_XM32, 0)
    // Three vectors, the first the destination: FMA's 213 forms (a * b + c), VPERMT2's and the dot products; and the blends by a
    // vector's signs.
    BUSTER_CHECK_X86_SSE("__builtin_ia32_vfmaddsubpd", CHECK_X86_LOWER_TERNARY, 8, false, VFMADDSUB213PD, X_X_XM128_EVEX, Y_Y_YM256_EVEX,
                         BUSTER_X86_FEATURE(FMA))
    BUSTER_CHECK_X86_SSE("__builtin_ia32_vfmaddsubps", CHECK_X86_LOWER_TERNARY, 4, false, VFMADDSUB213PS, X_X_XM128_EVEX, Y_Y_YM256_EVEX,
                         BUSTER_X86_FEATURE(FMA))
    BUSTER_CHECK_X86_ONE("vpermi2vard128", TERNARY, 4, 16, VPERMT2D_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("vpermi2vard256", TERNARY, 4, 32, VPERMT2D_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("vpermi2varq128", TERNARY, 8, 16, VPERMT2Q_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("vpermi2varq256", TERNARY, 8, 32, VPERMT2Q_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("vpermi2varhi128", TERNARY, 2, 16, VPERMT2W_X_X_XM128, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_ONE("vpermi2varhi256", TERNARY, 2, 32, VPERMT2W_Y_Y_YM256, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpermi2varpd", CHECK_X86_LOWER_TERNARY, 8, false, VPERMT2PD, X_X_XM128, Y_Y_YM256, Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpermi2varps", CHECK_X86_LOWER_TERNARY, 4, false, VPERMT2PS, X_X_XM128, Y_Y_YM256, Z_Z_ZM512, 0)
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpmadd52huq", CHECK_X86_LOWER_TERNARY, 8, false, VPMADD52HUQ, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512IFMA))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_vpmadd52luq", CHECK_X86_LOWER_TERNARY, 8, false, VPMADD52LUQ, X_X_XM128_EVEX, Y_Y_YM256_EVEX, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512IFMA))
    BUSTER_CHECK_X86_FORMS("__builtin_ia32_dpbf16ps_", CHECK_X86_LOWER_TERNARY, 4, false, VDPBF16PS, X_X_XM128, Y_Y_YM256, Z_Z_ZM512,
                           BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_SSE("__builtin_ia32_blendvpd", CHECK_X86_LOWER_BLENDV, 8, false, VBLENDVPD, X_X_XM128_X, Y_Y_YM256_Y, 0)
    BUSTER_CHECK_X86_SSE("__builtin_ia32_blendvps", CHECK_X86_LOWER_BLENDV, 4, false, VBLENDVPS, X_X_XM128_X, Y_Y_YM256_Y, 0)
    // One vector's operation (a conversion's of other widths) merged into w under the mask, with a rounding control where
    // the builtin's last argument is one.
    BUSTER_CHECK_X86_ONE("cvtpd2dq128_mask", MASK_UNARY, 8, 16, VCVTPD2DQ_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("cvtpd2ps_mask", MASK_UNARY, 8, 16, VCVTPD2PS_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("cvtpd2qq128_mask", MASK_UNARY, 8, 16, VCVTPD2QQ_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtpd2qq256_mask", MASK_UNARY, 8, 32, VCVTPD2QQ_Y_YM256, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtpd2uqq128_mask", MASK_UNARY, 8, 16, VCVTPD2UQQ_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtpd2uqq256_mask", MASK_UNARY, 8, 32, VCVTPD2UQQ_Y_YM256, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttpd2qq128_mask", MASK_UNARY, 8, 16, VCVTTPD2QQ_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttpd2qq256_mask", MASK_UNARY, 8, 32, VCVTTPD2QQ_Y_YM256, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttpd2uqq128_mask", MASK_UNARY, 8, 16, VCVTTPD2UQQ_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttpd2uqq256_mask", MASK_UNARY, 8, 32, VCVTTPD2UQQ_Y_YM256, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtpd2udq128_mask", MASK_UNARY, 8, 16, VCVTPD2UDQ_X_XM128, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtpd2udq256_mask", MASK_UNARY, 8, 32, false, VCVTPD2UDQ_X_YM256, 16, 0)
    BUSTER_CHECK_X86_ONE("cvttpd2udq128_mask", MASK_UNARY, 8, 16, VCVTTPD2UDQ_X_XM128, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvttpd2udq256_mask", MASK_UNARY, 8, 32, false, VCVTTPD2UDQ_X_YM256, 16, 0)
    BUSTER_CHECK_X86_ONE("cvttpd2dq128_mask", MASK_UNARY, 8, 16, VCVTTPD2DQ_X_XM128_EVEX, 0)
    BUSTER_CHECK_X86_ONE("cvtps2udq128_mask", MASK_UNARY, 4, 16, VCVTPS2UDQ_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("cvtps2udq256_mask", MASK_UNARY, 4, 32, VCVTPS2UDQ_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("cvttps2udq128_mask", MASK_UNARY, 4, 16, VCVTTPS2UDQ_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("cvttps2udq256_mask", MASK_UNARY, 4, 32, VCVTTPS2UDQ_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("cvtps2qq128_mask", MASK_UNARY, 8, 16, VCVTPS2QQ_X_XM64, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE_OTHER("cvtps2qq256_mask", MASK_UNARY, 4, 16, false, VCVTPS2QQ_Y_XM128, 32, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtps2uqq128_mask", MASK_UNARY, 8, 16, VCVTPS2UQQ_X_XM64, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE_OTHER("cvtps2uqq256_mask", MASK_UNARY, 4, 16, false, VCVTPS2UQQ_Y_XM128, 32, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttps2qq128_mask", MASK_UNARY, 8, 16, VCVTTPS2QQ_X_XM64, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE_OTHER("cvttps2qq256_mask", MASK_UNARY, 4, 16, false, VCVTTPS2QQ_Y_XM128, 32, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvttps2uqq128_mask", MASK_UNARY, 8, 16, VCVTTPS2UQQ_X_XM64, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE_OTHER("cvttps2uqq256_mask", MASK_UNARY, 4, 16, false, VCVTTPS2UQQ_Y_XM128, 32, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtqq2ps128_mask", MASK_UNARY, 8, 16, VCVTQQ2PS_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("cvtuqq2ps128_mask", MASK_UNARY, 8, 16, VCVTUQQ2PS_X_XM128, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("getexppd128_mask", MASK_UNARY, 8, 16, VGETEXPPD_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("getexppd256_mask", MASK_UNARY, 8, 32, VGETEXPPD_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("getexpps128_mask", MASK_UNARY, 4, 16, VGETEXPPS_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("getexpps256_mask", MASK_UNARY, 4, 32, VGETEXPPS_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("rcp14pd128_mask", MASK_UNARY, 8, 16, VRCP14PD_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("rcp14pd256_mask", MASK_UNARY, 8, 32, VRCP14PD_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("rcp14pd512_mask", MASK_UNARY, 8, 64, VRCP14PD_Z_ZM512, 0)
    BUSTER_CHECK_X86_ONE("rcp14ps128_mask", MASK_UNARY, 4, 16, VRCP14PS_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("rcp14ps256_mask", MASK_UNARY, 4, 32, VRCP14PS_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("rcp14ps512_mask", MASK_UNARY, 4, 64, VRCP14PS_Z_ZM512, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14pd128_mask", MASK_UNARY, 8, 16, VRSQRT14PD_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14pd256_mask", MASK_UNARY, 8, 32, VRSQRT14PD_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14pd512_mask", MASK_UNARY, 8, 64, VRSQRT14PD_Z_ZM512, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14ps128_mask", MASK_UNARY, 4, 16, VRSQRT14PS_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14ps256_mask", MASK_UNARY, 4, 32, VRSQRT14PS_Y_YM256, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14ps512_mask", MASK_UNARY, 4, 64, VRSQRT14PS_Z_ZM512, 0)
    BUSTER_CHECK_X86_ONE("vcvtph2ps_mask", MASK_UNARY, 4, 16, VCVTPH2PS_X_XM64_EVEX, 0)
    BUSTER_CHECK_X86_ONE_OTHER("vcvtph2ps256_mask", MASK_UNARY, 2, 16, false, VCVTPH2PS_Y_XM128_EVEX, 32, 0)
    BUSTER_CHECK_X86_ROUNDED("vcvtph2ps512_mask", MASK_UNARY, 2, 32, VCVTPH2PS_Z_YM256, 64, 0, VCVTPH2PS_Z_Y_SAE, SAE)
    BUSTER_CHECK_X86_ONE("cvtneps2bf16_128_mask", MASK_UNARY, 4, 16, VCVTNEPS2BF16_X_XM128_EVEX, BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ONE_OTHER("cvtneps2bf16_256_mask", MASK_UNARY, 4, 32, false, VCVTNEPS2BF16_X_YM256_EVEX, 16, BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ONE_OTHER("cvtneps2bf16_512_mask", MASK_UNARY, 4, 64, false, VCVTNEPS2BF16_Y_ZM512, 32, BUSTER_X86_FEATURE(AVX512BF16))
    BUSTER_CHECK_X86_ROUNDED("cvtpd2dq512_mask", MASK_UNARY, 8, 64, VCVTPD2DQ_Y_ZM512, 32, 0, VCVTPD2DQ_Y_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtpd2ps512_mask", MASK_UNARY, 8, 64, VCVTPD2PS_Y_ZM512, 32, 0, VCVTPD2PS_Y_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtpd2udq512_mask", MASK_UNARY, 8, 64, VCVTPD2UDQ_Y_ZM512, 32, 0, VCVTPD2UDQ_Y_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtqq2ps512_mask", MASK_UNARY, 8, 64, VCVTQQ2PS_Y_ZM512, 32, BUSTER_X86_FEATURE(AVX512DQ), VCVTQQ2PS_Y_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvttpd2dq512_mask", MASK_UNARY, 8, 64, VCVTTPD2DQ_Y_ZM512, 32, 0, VCVTTPD2DQ_Y_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvttpd2udq512_mask", MASK_UNARY, 8, 64, VCVTTPD2UDQ_Y_ZM512, 32, 0, VCVTTPD2UDQ_Y_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvtuqq2ps512_mask", MASK_UNARY, 8, 64, VCVTUQQ2PS_Y_ZM512, 32, BUSTER_X86_FEATURE(AVX512DQ), VCVTUQQ2PS_Y_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtpd2qq512_mask", MASK_UNARY, 8, 64, VCVTPD2QQ_Z_ZM512, 0, BUSTER_X86_FEATURE(AVX512DQ), VCVTPD2QQ_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtpd2uqq512_mask", MASK_UNARY, 8, 64, VCVTPD2UQQ_Z_ZM512, 0, BUSTER_X86_FEATURE(AVX512DQ), VCVTPD2UQQ_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvttpd2qq512_mask", MASK_UNARY, 8, 64, VCVTTPD2QQ_Z_ZM512, 0, BUSTER_X86_FEATURE(AVX512DQ), VCVTTPD2QQ_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvttpd2uqq512_mask", MASK_UNARY, 8, 64, VCVTTPD2UQQ_Z_ZM512, 0, BUSTER_X86_FEATURE(AVX512DQ), VCVTTPD2UQQ_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("getexppd512_mask", MASK_UNARY, 8, 64, VGETEXPPD_Z_ZM512, 0, 0, VGETEXPPD_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvtps2dq512_mask", MASK_UNARY, 4, 64, VCVTPS2DQ_Z_ZM512, 0, 0, VCVTPS2DQ_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtps2udq512_mask", MASK_UNARY, 4, 64, VCVTPS2UDQ_Z_ZM512, 0, 0, VCVTPS2UDQ_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvttps2dq512_mask", MASK_UNARY, 4, 64, VCVTTPS2DQ_Z_ZM512, 0, 0, VCVTTPS2DQ_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvttps2udq512_mask", MASK_UNARY, 4, 64, VCVTTPS2UDQ_Z_ZM512, 0, 0, VCVTTPS2UDQ_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("getexpps512_mask", MASK_UNARY, 4, 64, VGETEXPPS_Z_ZM512, 0, 0, VGETEXPPS_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvtps2qq512_mask", MASK_UNARY, 4, 32, VCVTPS2QQ_Z_YM256, 64, BUSTER_X86_FEATURE(AVX512DQ), VCVTPS2QQ_Z_Y_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtps2uqq512_mask", MASK_UNARY, 4, 32, VCVTPS2UQQ_Z_YM256, 64, BUSTER_X86_FEATURE(AVX512DQ), VCVTPS2UQQ_Z_Y_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvttps2qq512_mask", MASK_UNARY, 4, 32, VCVTTPS2QQ_Z_YM256, 64, BUSTER_X86_FEATURE(AVX512DQ), VCVTTPS2QQ_Z_Y_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("cvttps2uqq512_mask", MASK_UNARY, 4, 32, VCVTTPS2UQQ_Z_YM256, 64, BUSTER_X86_FEATURE(AVX512DQ), VCVTTPS2UQQ_Z_Y_SAE, SAE)
    // Two vectors' operation merged into w under the mask, the scalar ones' other elements a's.
    BUSTER_CHECK_X86_ONE("scalefpd128_mask", MASK_MERGE, 8, 16, VSCALEFPD_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("scalefpd256_mask", MASK_MERGE, 8, 32, VSCALEFPD_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ROUNDED("scalefpd512_mask", MASK_MERGE, 8, 64, VSCALEFPD_Z_Z_ZM512, 0, 0, VSCALEFPD_Z_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ONE("scalefps128_mask", MASK_MERGE, 4, 16, VSCALEFPS_X_X_XM128, 0)
    BUSTER_CHECK_X86_ONE("scalefps256_mask", MASK_MERGE, 4, 32, VSCALEFPS_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_ROUNDED("scalefps512_mask", MASK_MERGE, 4, 64, VSCALEFPS_Z_Z_ZM512, 0, 0, VSCALEFPS_Z_Z_Z_ER, ER)
    BUSTER_CHECK_X86_ONE("rcp14sd_mask", MASK_MERGE, 8, 16, VRCP14SD_X_X_XM64, 0)
    BUSTER_CHECK_X86_ONE("rcp14ss_mask", MASK_MERGE, 4, 16, VRCP14SS_X_X_XM32, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14sd_mask", MASK_MERGE, 8, 16, VRSQRT14SD_X_X_XM64, 0)
    BUSTER_CHECK_X86_ONE("rsqrt14ss_mask", MASK_MERGE, 4, 16, VRSQRT14SS_X_X_XM32, 0)
    BUSTER_CHECK_X86_ROUNDED("maxsd_round_mask", MASK_MERGE, 8, 16, VMAXSD_X_X_XM64_EVEX, 0, 0, VMAXSD_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("maxss_round_mask", MASK_MERGE, 4, 16, VMAXSS_X_X_XM32_EVEX, 0, 0, VMAXSS_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("minsd_round_mask", MASK_MERGE, 8, 16, VMINSD_X_X_XM64_EVEX, 0, 0, VMINSD_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("minss_round_mask", MASK_MERGE, 4, 16, VMINSS_X_X_XM32_EVEX, 0, 0, VMINSS_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("getexpsd128_round_mask", MASK_MERGE, 8, 16, VGETEXPSD_X_X_XM64, 0, 0, VGETEXPSD_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("getexpss128_round_mask", MASK_MERGE, 4, 16, VGETEXPSS_X_X_XM32, 0, 0, VGETEXPSS_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("scalefsd_round_mask", MASK_MERGE, 8, 16, VSCALEFSD_X_X_XM64, 0, 0, VSCALEFSD_X_X_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("scalefss_round_mask", MASK_MERGE, 4, 16, VSCALEFSS_X_X_XM32, 0, 0, VSCALEFSS_X_X_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("sqrtsd_round_mask", MASK_MERGE, 8, 16, VSQRTSD_X_X_XM64_EVEX, 0, 0, VSQRTSD_X_X_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("sqrtss_round_mask", MASK_MERGE, 4, 16, VSQRTSS_X_X_XM32_EVEX, 0, 0, VSQRTSS_X_X_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtsd2ss_round_mask", MASK_MERGE, 4, 16, VCVTSD2SS_X_X_XM64_EVEX, 0, 0, VCVTSD2SS_X_X_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("cvtss2sd_round_mask", MASK_MERGE, 8, 16, VCVTSS2SD_X_X_XM32_EVEX, 0, 0, VCVTSS2SD_X_X_X_SAE, SAE)
    // Two vectors with the exceptions' suppression where their last argument asks for it.
    BUSTER_CHECK_X86_ROUNDED("maxpd512", BINARY, 8, 64, VMAXPD_Z_Z_ZM512, 0, 0, VMAXPD_Z_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("maxps512", BINARY, 4, 64, VMAXPS_Z_Z_ZM512, 0, 0, VMAXPS_Z_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("minpd512", BINARY, 8, 64, VMINPD_Z_Z_ZM512, 0, 0, VMINPD_Z_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("minps512", BINARY, 4, 64, VMINPS_Z_Z_ZM512, 0, 0, VMINPS_Z_Z_Z_SAE, SAE)
    // The SSE compares by the predicates their names spell, VEX's VCMP into vectors.
    BUSTER_CHECK_X86_PREDICATES("ps", 4, VCMPPS_X_X_XM128_IMM8)
    BUSTER_CHECK_X86_PREDICATES("pd", 8, VCMPPD_X_X_XM128_IMM8)
    BUSTER_CHECK_X86_PREDICATES("ss", 4, VCMPSS_X_X_XM32_IMM8)
    BUSTER_CHECK_X86_PREDICATES("sd", 8, VCMPSD_X_X_XM64_IMM8)
    // The compares and tests into an int, from the flags: the SSE compares' ordered and unordered forms, and the vectors'
    // and masks' tests, z of ZF, c of CF and nzc of neither.
    BUSTER_CHECK_X86_COMPARISONS("comi", VCOMISS_X_XM32_EVEX)
    BUSTER_CHECK_X86_COMPARISONS("comisd", VCOMISD_X_XM64_EVEX)
    BUSTER_CHECK_X86_COMPARISONS("ucomi", VUCOMISS_X_XM32_EVEX)
    BUSTER_CHECK_X86_COMPARISONS("ucomisd", VUCOMISD_X_XM64_EVEX)
    BUSTER_CHECK_X86_TEST("ptestz128", FLAGS, 16, false, VPTEST_X_XM128, E, 0, 0)
    BUSTER_CHECK_X86_TEST("ptestc128", FLAGS, 16, false, VPTEST_X_XM128, B, 0, 0)
    BUSTER_CHECK_X86_TEST("ptestnzc128", FLAGS, 16, false, VPTEST_X_XM128, A, 0, 0)
    BUSTER_CHECK_X86_TEST("ptestz256", FLAGS, 32, false, VPTEST_Y_YM256, E, 0, 0)
    BUSTER_CHECK_X86_TEST("ptestc256", FLAGS, 32, false, VPTEST_Y_YM256, B, 0, 0)
    BUSTER_CHECK_X86_TEST("ptestnzc256", FLAGS, 32, false, VPTEST_Y_YM256, A, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestzps", FLAGS, 16, false, VTESTPS_X_XM128, E, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestcps", FLAGS, 16, false, VTESTPS_X_XM128, B, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestnzcps", FLAGS, 16, false, VTESTPS_X_XM128, A, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestzps256", FLAGS, 32, false, VTESTPS_Y_YM256, E, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestcps256", FLAGS, 32, false, VTESTPS_Y_YM256, B, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestnzcps256", FLAGS, 32, false, VTESTPS_Y_YM256, A, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestzpd", FLAGS, 16, false, VTESTPD_X_XM128, E, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestcpd", FLAGS, 16, false, VTESTPD_X_XM128, B, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestnzcpd", FLAGS, 16, false, VTESTPD_X_XM128, A, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestzpd256", FLAGS, 32, false, VTESTPD_Y_YM256, E, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestcpd256", FLAGS, 32, false, VTESTPD_Y_YM256, B, 0, 0)
    BUSTER_CHECK_X86_TEST("vtestnzcpd256", FLAGS, 32, false, VTESTPD_Y_YM256, A, 0, 0)
    BUSTER_CHECK_X86_TEST("kortestzqi", KTEST, 1, false, KORTESTB_K_K, E, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("kortestzhi", KTEST, 2, false, KORTESTW_K_K, E, 0, 0)
    BUSTER_CHECK_X86_TEST("kortestzsi", KTEST, 4, false, KORTESTD_K_K, E, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("kortestzdi", KTEST, 8, false, KORTESTQ_K_K, E, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("kortestcqi", KTEST, 1, false, KORTESTB_K_K, B, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("kortestchi", KTEST, 2, false, KORTESTW_K_K, B, 0, 0)
    BUSTER_CHECK_X86_TEST("kortestcsi", KTEST, 4, false, KORTESTD_K_K, B, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("kortestcdi", KTEST, 8, false, KORTESTQ_K_K, B, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("ktestzqi", KTEST, 1, false, KTESTB_K_K, E, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("ktestzhi", KTEST, 2, false, KTESTW_K_K, E, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("ktestzsi", KTEST, 4, false, KTESTD_K_K, E, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("ktestcqi", KTEST, 1, false, KTESTB_K_K, B, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("ktestchi", KTEST, 2, false, KTESTW_K_K, B, 0, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_TEST("ktestcsi", KTEST, 4, false, KTESTD_K_K, B, 0, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_TEST("ktestcdi", KTEST, 8, false, KTESTQ_K_K, B, 0, BUSTER_CHECK_X86_BW)
    // The masks' other operations, of each width: KNOT's of one, and two's.
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_knotqi", CHECK_X86_LOWER_KUNARY, 1, X86_FORM_KNOTB_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_knothi", CHECK_X86_LOWER_KUNARY, 2, X86_FORM_KNOTW_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_knotsi", CHECK_X86_LOWER_KUNARY, 4, X86_FORM_KNOTD_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_knotdi", CHECK_X86_LOWER_KUNARY, 8, X86_FORM_KNOTQ_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kaddqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KADDB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kaddsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KADDD_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kadddi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KADDQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KANDB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KANDD_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kanddi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KANDQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandnqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KANDNB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandnhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KANDNW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandnsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KANDND_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kandndi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KANDNQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_korqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KORB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_korhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KORW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_korsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KORD_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxnorqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KXNORB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxnorhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KXNORW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxnorsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KXNORD_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxnordi", CHECK_X86_LOWER_KBINARY, 8, X86_FORM_KXNORQ_K_K_K, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxorqi", CHECK_X86_LOWER_KBINARY, 1, X86_FORM_KXORB_K_K_K, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxorhi", CHECK_X86_LOWER_KBINARY, 2, X86_FORM_KXORW_K_K_K, false, 0)
    BUSTER_CHECK_X86_RULE_MASK("__builtin_ia32_kxorsi", CHECK_X86_LOWER_KBINARY, 4, X86_FORM_KXORD_K_K_K, false, BUSTER_CHECK_X86_BW)
    // The general registers' and memory's instructions: the CRC and the bit fields; the FS and GS bases,
    // the processor's id and the shadow stack's pointer; the caches' lines, the x87 and SSE state's save (its restore,
    // which writes the XMM registers floating values live in, is a leaf's: c_builtin_x86_helpers.c), the non-temporal and
    // direct stores, the TLB's and the caches' invalidations and the shadow stack's own; prefetchi's hint its form's
    // (PREFETCHIT0's 3, PREFETCHIT1's 2).
    BUSTER_CHECK_X86_SYSTEM("crc32qi", 4, CRC32_R32_RM8, ACCUMULATE, BUSTER_X86_FEATURE(CRC32))
    BUSTER_CHECK_X86_SYSTEM("crc32hi", 4, CRC32_R32_RM16, ACCUMULATE, BUSTER_X86_FEATURE(CRC32))
    BUSTER_CHECK_X86_SYSTEM("crc32si", 4, CRC32_R32_RM32, ACCUMULATE, BUSTER_X86_FEATURE(CRC32))
    BUSTER_CHECK_X86_SYSTEM("crc32di", 8, CRC32_R64_RM64, ACCUMULATE, BUSTER_X86_FEATURE(CRC32))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_bextr_u32", CHECK_X86_LOWER_GENERAL, 4, 4, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_BEXTR_R32_RM32_R32,
                          BUSTER_X86_FEATURE(BMI))
    BUSTER_CHECK_X86_RULE("__builtin_ia32_bextr_u64", CHECK_X86_LOWER_GENERAL, 8, 8, false, true, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_BEXTR_R64_RM64_R64,
                          BUSTER_X86_FEATURE(BMI))
    BUSTER_CHECK_X86_SYSTEM("rdfsbase32", 4, RDFSBASE_R32, READ, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("rdfsbase64", 8, RDFSBASE_R64, READ, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("rdgsbase32", 4, RDGSBASE_R32, READ, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("rdgsbase64", 8, RDGSBASE_R64, READ, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("wrfsbase32", 4, WRFSBASE_R32, WRITE, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("wrfsbase64", 8, WRFSBASE_R64, WRITE, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("wrgsbase32", 4, WRGSBASE_R32, WRITE, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("wrgsbase64", 8, WRGSBASE_R64, WRITE, BUSTER_X86_FEATURE(FSGSBASE))
    BUSTER_CHECK_X86_SYSTEM("rdpid", 4, RDPID_R64, READ, BUSTER_X86_FEATURE(RDPID))
    BUSTER_CHECK_X86_SYSTEM("rdsspd", 4, RDSSPD_R32, MODIFY, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("rdsspq", 8, RDSSPQ_R64, MODIFY, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("incsspd", 4, INCSSPD_R32, WRITE, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("incsspq", 8, INCSSPQ_R64, WRITE, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("clflushopt", 8, CLFLUSHOPT_M8, MEMORY, BUSTER_X86_FEATURE(CLFLUSHOPT))
    BUSTER_CHECK_X86_SYSTEM("clwb", 8, CLWB_M8, MEMORY, BUSTER_X86_FEATURE(CLWB))
    BUSTER_CHECK_X86_SYSTEM("fxsave", 8, FXSAVE_M, MEMORY, 0)
    BUSTER_CHECK_X86_SYSTEM("fxsave64", 8, FXSAVE64_M, MEMORY, 0)
    BUSTER_CHECK_X86_SYSTEM("rstorssp", 8, RSTORSSP_M64, MEMORY, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("clrssbsy", 8, CLRSSBSY_M64, MEMORY, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_prefetchi", CHECK_X86_LOWER_SYSTEM, 8, 8, false, 1, X86_FORM_PREFETCHIT0_M8, CHECK_X86_LAYOUT_MEMORY,
                                BUSTER_X86_FEATURE(PREFETCHI))
    BUSTER_CHECK_X86_SYSTEM("movnti", 4, MOVNTI_M32_R32, STORE, 0)
    BUSTER_CHECK_X86_SYSTEM("movnti64", 8, MOVNTI_M64_R64, STORE, 0)
    BUSTER_CHECK_X86_SYSTEM("directstore_u32", 4, MOVDIRI_M32_R32, STORE, BUSTER_X86_FEATURE(MOVDIRI))
    BUSTER_CHECK_X86_SYSTEM("directstore_u64", 8, MOVDIRI_M64_R64, STORE, BUSTER_X86_FEATURE(MOVDIRI))
    BUSTER_CHECK_X86_SYSTEM("wrssd", 4, WRSSD_M32_R32, STORE_VALUE_FIRST, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("wrssq", 8, WRSSQ_M64_R64, STORE_VALUE_FIRST, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("wrussd", 4, WRUSSD_M32_R32, STORE_VALUE_FIRST, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("wrussq", 8, WRUSSQ_M64_R64, STORE_VALUE_FIRST, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("movdir64b", 8, MOVDIR64B_R64_M512, POINTERS, BUSTER_X86_FEATURE(MOVDIR64B))
    BUSTER_CHECK_X86_SYSTEM("invpcid", 8, INVPCID_R64_M128, WIDENED, BUSTER_X86_FEATURE(INVPCID))
    BUSTER_CHECK_X86_SYSTEM("emms", 4, EMMS, NONE, 0)
    BUSTER_CHECK_X86_SYSTEM("wbinvd", 4, WBINVD, NONE, 0)
    BUSTER_CHECK_X86_SYSTEM("wbnoinvd", 4, WBNOINVD, NONE, BUSTER_X86_FEATURE(WBNOINVD))
    BUSTER_CHECK_X86_SYSTEM("saveprevssp", 4, SAVEPREVSSP, NONE, BUSTER_X86_FEATURE(SHSTK))
    BUSTER_CHECK_X86_SYSTEM("setssbsy", 4, SETSSBSY, NONE, BUSTER_X86_FEATURE(SHSTK))
    // The random numbers, stored with whether they are (CF), and the sums and differences with carries.
    BUSTER_CHECK_X86_ONE("rdrand16_step", RANDOM, 2, 2, RDRAND_R16, BUSTER_X86_FEATURE(RDRND))
    BUSTER_CHECK_X86_ONE("rdrand32_step", RANDOM, 4, 4, RDRAND_R32, BUSTER_X86_FEATURE(RDRND))
    BUSTER_CHECK_X86_ONE("rdrand64_step", RANDOM, 8, 8, RDRAND_R64, BUSTER_X86_FEATURE(RDRND))
    BUSTER_CHECK_X86_ONE("rdseed16_step", RANDOM, 2, 2, RDSEED_R16, BUSTER_X86_FEATURE(RDSEED))
    BUSTER_CHECK_X86_ONE("rdseed32_step", RANDOM, 4, 4, RDSEED_R32, BUSTER_X86_FEATURE(RDSEED))
    BUSTER_CHECK_X86_ONE("rdseed64_step", RANDOM, 8, 8, RDSEED_R64, BUSTER_X86_FEATURE(RDSEED))
    BUSTER_CHECK_X86_ONE("addcarryx_u32", CARRY, 4, 4, ADC_R32_RM32, 0)
    BUSTER_CHECK_X86_ONE("addcarryx_u64", CARRY, 8, 8, ADC_R64_RM64, 0)
    BUSTER_CHECK_X86_ONE("subborrow_u32", CARRY, 4, 4, SBB_R32_RM32, 0)
    BUSTER_CHECK_X86_ONE("subborrow_u64", CARRY, 8, 8, SBB_R64_RM64, 0)
    // The vectors' loads and stores of their own: under a vector's signs, the unaligned load, SSE4A's non-temporal scalar
    // stores and the byte stores under a vector's signs.
    BUSTER_CHECK_X86_MEMORY("maskloadd", 4, 16, VPMASKMOVD_X_X_M128, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadd256", 4, 32, VPMASKMOVD_Y_Y_M256, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadq", 8, 16, VPMASKMOVQ_X_X_M128, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadq256", 8, 32, VPMASKMOVQ_Y_Y_M256, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadps", 4, 16, VMASKMOVPS_X_X_M128, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadps256", 4, 32, VMASKMOVPS_Y_Y_M256, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadpd", 8, 16, VMASKMOVPD_X_X_M128, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskloadpd256", 8, 32, VMASKMOVPD_Y_Y_M256, MASKED_LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("maskstored", 4, 16, VPMASKMOVD_M128_X_X, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstored256", 4, 32, VPMASKMOVD_M256_Y_Y, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstoreq", 8, 16, VPMASKMOVQ_M128_X_X, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstoreq256", 8, 32, VPMASKMOVQ_M256_Y_Y, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstoreps", 4, 16, VMASKMOVPS_M128_X_X, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstoreps256", 4, 32, VMASKMOVPS_M256_Y_Y, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstorepd", 8, 16, VMASKMOVPD_M128_X_X, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("maskstorepd256", 8, 32, VMASKMOVPD_M256_Y_Y, MASKED_STORE, 0)
    BUSTER_CHECK_X86_MEMORY("lddqu", 1, 16, VLDDQU_X_M128, LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("lddqu256", 1, 32, VLDDQU_Y_M256, LOAD, 0)
    BUSTER_CHECK_X86_MEMORY("movntsd", 8, 16, MOVNTSD_M64_X, STORE_VECTOR, BUSTER_X86_FEATURE(SSE4A))
    BUSTER_CHECK_X86_MEMORY("movntss", 4, 16, MOVNTSS_M32_X, STORE_VECTOR, BUSTER_X86_FEATURE(SSE4A))
    BUSTER_CHECK_X86_MEMORY("maskmovdqu", 1, 16, VMOVDQU8_XM128_X, BYTES_STORE, BUSTER_CHECK_X86_BW)
    // The FMAs under a mask: merged into a, a zeroed and merged into c, of the scalars and of 64 bytes' alternating sums.
    BUSTER_CHECK_X86_FMA("vfmaddsd3_mask", 8, 16, VFMADD213SD_X_X_XM64_EVEX, VFMADD213SD_X_X_X_ER, A)
    BUSTER_CHECK_X86_FMA("vfmaddsd3_maskz", 8, 16, VFMADD213SD_X_X_XM64_EVEX, VFMADD213SD_X_X_X_ER, A_ZEROED)
    BUSTER_CHECK_X86_FMA("vfmaddsd3_mask3", 8, 16, VFMADD231SD_X_X_XM64_EVEX, VFMADD231SD_X_X_X_ER, C)
    BUSTER_CHECK_X86_FMA("vfmaddss3_mask", 4, 16, VFMADD213SS_X_X_XM32_EVEX, VFMADD213SS_X_X_X_ER, A)
    BUSTER_CHECK_X86_FMA("vfmaddss3_maskz", 4, 16, VFMADD213SS_X_X_XM32_EVEX, VFMADD213SS_X_X_X_ER, A_ZEROED)
    BUSTER_CHECK_X86_FMA("vfmaddss3_mask3", 4, 16, VFMADD231SS_X_X_XM32_EVEX, VFMADD231SS_X_X_X_ER, C)
    BUSTER_CHECK_X86_FMA("vfmsubsd3_mask3", 8, 16, VFMSUB231SD_X_X_XM64_EVEX, VFMSUB231SD_X_X_X_ER, C)
    BUSTER_CHECK_X86_FMA("vfmsubss3_mask3", 4, 16, VFMSUB231SS_X_X_XM32_EVEX, VFMSUB231SS_X_X_X_ER, C)
    BUSTER_CHECK_X86_FMA("vfmaddsubpd512_mask", 8, 64, VFMADDSUB213PD_Z_Z_ZM512, VFMADDSUB213PD_Z_Z_Z_ER, A)
    BUSTER_CHECK_X86_FMA("vfmaddsubpd512_maskz", 8, 64, VFMADDSUB213PD_Z_Z_ZM512, VFMADDSUB213PD_Z_Z_Z_ER, A_ZEROED)
    BUSTER_CHECK_X86_FMA("vfmaddsubpd512_mask3", 8, 64, VFMADDSUB231PD_Z_Z_ZM512, VFMADDSUB231PD_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_FMA("vfmaddsubps512_mask", 4, 64, VFMADDSUB213PS_Z_Z_ZM512, VFMADDSUB213PS_Z_Z_Z_ER, A)
    BUSTER_CHECK_X86_FMA("vfmaddsubps512_maskz", 4, 64, VFMADDSUB213PS_Z_Z_ZM512, VFMADDSUB213PS_Z_Z_Z_ER, A_ZEROED)
    BUSTER_CHECK_X86_FMA("vfmaddsubps512_mask3", 4, 64, VFMADDSUB231PS_Z_Z_ZM512, VFMADDSUB231PS_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_FMA("vfmsubaddpd512_mask3", 8, 64, VFMSUBADD231PD_Z_Z_ZM512, VFMSUBADD231PD_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_FMA("vfmsubaddps512_mask3", 4, 64, VFMSUBADD231PS_Z_Z_ZM512, VFMSUBADD231PS_Z_Z_Z_ER, C)
    // The floating reductions of 64 bytes.
    BUSTER_CHECK_X86_REDUCE("fadd_pd512", 8, ADD)
    BUSTER_CHECK_X86_REDUCE("fadd_ps512", 4, ADD)
    BUSTER_CHECK_X86_REDUCE("fmul_pd512", 8, MUL)
    BUSTER_CHECK_X86_REDUCE("fmul_ps512", 4, MUL)
    BUSTER_CHECK_X86_REDUCE("fmax_pd512", 8, MAX)
    BUSTER_CHECK_X86_REDUCE("fmax_ps512", 4, MAX)
    BUSTER_CHECK_X86_REDUCE("fmin_pd512", 8, MIN)
    BUSTER_CHECK_X86_REDUCE("fmin_ps512", 4, MIN)
    // The scalar conversions to integers: by MXCSR's rounding, of a rounding control, or truncating, with the exceptions'
    // suppression where their last argument asks for it.
    BUSTER_CHECK_X86_ONE_OTHER("cvtsd2si", CONVERT, 8, 16, false, VCVTSD2SI_R32_XM64_EVEX, 4, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtsd2si64", CONVERT, 8, 16, false, VCVTSD2SI_R64_XM64_EVEX, 8, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtss2si", CONVERT, 4, 16, false, VCVTSS2SI_R32_XM32_EVEX, 4, 0)
    BUSTER_CHECK_X86_ONE_OTHER("cvtss2si64", CONVERT, 4, 16, false, VCVTSS2SI_R64_XM32_EVEX, 8, 0)
    BUSTER_CHECK_X86_ROUNDED("vcvtsd2usi32", CONVERT, 8, 16, VCVTSD2USI_R32_XM64, 4, 0, VCVTSD2USI_R32_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("vcvtsd2usi64", CONVERT, 8, 16, VCVTSD2USI_R64_XM64, 8, 0, VCVTSD2USI_R64_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("vcvtss2usi32", CONVERT, 4, 16, VCVTSS2USI_R32_XM32, 4, 0, VCVTSS2USI_R32_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("vcvtss2usi64", CONVERT, 4, 16, VCVTSS2USI_R64_XM32, 8, 0, VCVTSS2USI_R64_X_ER, ER)
    BUSTER_CHECK_X86_ROUNDED("vcvttsd2si32", CONVERT, 8, 16, VCVTTSD2SI_R32_XM64_EVEX, 4, 0, VCVTTSD2SI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttsd2si64", CONVERT, 8, 16, VCVTTSD2SI_R64_XM64_EVEX, 8, 0, VCVTTSD2SI_R64_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttss2si32", CONVERT, 4, 16, VCVTTSS2SI_R32_XM32_EVEX, 4, 0, VCVTTSS2SI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttss2si64", CONVERT, 4, 16, VCVTTSS2SI_R64_XM32_EVEX, 8, 0, VCVTTSS2SI_R64_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttsd2usi32", CONVERT, 8, 16, VCVTTSD2USI_R32_XM64, 4, 0, VCVTTSD2USI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttsd2usi64", CONVERT, 8, 16, VCVTTSD2USI_R64_XM64, 8, 0, VCVTTSD2USI_R64_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttss2usi32", CONVERT, 4, 16, VCVTTSS2USI_R32_XM32, 4, 0, VCVTTSS2USI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_ROUNDED("vcvttss2usi64", CONVERT, 4, 16, VCVTTSS2USI_R64_XM32, 8, 0, VCVTTSS2USI_R64_X_SAE, SAE)
    // The rest: AVX's lanes of 32 bytes, the rounding to a scale under a mask, bf16's scalar select, SSE4A's bit fields and
    // SHA's messages and rounds of SHA-1.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vextractf128_pd256", CHECK_X86_LOWER_UNARY_IMMEDIATE, 8, 32, false, 1, X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8,
                                16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vextractf128_ps256", CHECK_X86_LOWER_UNARY_IMMEDIATE, 4, 32, false, 1, X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8,
                                16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vextractf128_si256", CHECK_X86_LOWER_UNARY_IMMEDIATE, 4, 32, false, 1, X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8,
                                16, 0)
    BUSTER_CHECK_X86_ROUNDED_IMMEDIATE("rndscalepd_mask", MASK_UNARY, 8, 64, 1, VRNDSCALEPD_Z_ZM512_IMM8, 0, VRNDSCALEPD_Z_Z_SAE_IMM8, SAE)
    BUSTER_CHECK_X86_ROUNDED_IMMEDIATE("rndscaleps_mask", MASK_UNARY, 4, 64, 1, VRNDSCALEPS_Z_ZM512_IMM8, 0, VRNDSCALEPS_Z_Z_SAE_IMM8, SAE)
    BUSTER_CHECK_X86_ONE("selectsbf_128", SELECT_SCALAR, 2, 16, NONE, 0)
    BUSTER_CHECK_X86_ONE("extrq", BINARY, 8, 16, EXTRQ_X_X, BUSTER_X86_FEATURE(SSE4A))
    BUSTER_CHECK_X86_ONE("insertq", BINARY, 8, 16, INSERTQ_X_X, BUSTER_X86_FEATURE(SSE4A))
    BUSTER_CHECK_X86_ONE("sha1msg1", BINARY, 4, 16, SHA1MSG1_X_XM128, BUSTER_X86_FEATURE(SHA))
    BUSTER_CHECK_X86_ONE("sha1msg2", BINARY, 4, 16, SHA1MSG2_X_XM128, BUSTER_X86_FEATURE(SHA))
    BUSTER_CHECK_X86_ONE("sha1nexte", BINARY, 4, 16, SHA1NEXTE_X_XM128, BUSTER_X86_FEATURE(SHA))
    BUSTER_CHECK_X86_ONE("sha256msg1", BINARY, 4, 16, SHA256MSG1_X_XM128, BUSTER_X86_FEATURE(SHA))
    BUSTER_CHECK_X86_ONE("sha256msg2", BINARY, 4, 16, SHA256MSG2_X_XM128, BUSTER_X86_FEATURE(SHA))
    // The intersections' pairs of masks, stored by KMOVB (AVX512-DQ's) or, of 16 lanes, KMOVW.
    BUSTER_CHECK_X86_ONE("vp2intersect_d_128", INTERSECT, 4, 16, VP2INTERSECTD_K_X_XM128, BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("vp2intersect_d_256", INTERSECT, 4, 32, VP2INTERSECTD_K_Y_YM256, BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("vp2intersect_d_512", INTERSECT, 4, 64, VP2INTERSECTD_K_Z_ZM512, BUSTER_X86_FEATURE(AVX512VP2INTERSECT))
    BUSTER_CHECK_X86_ONE("vp2intersect_q_128", INTERSECT, 8, 16, VP2INTERSECTQ_K_X_XM128, BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("vp2intersect_q_256", INTERSECT, 8, 32, VP2INTERSECTQ_K_Y_YM256, BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_ONE("vp2intersect_q_512", INTERSECT, 8, 64, VP2INTERSECTQ_K_Z_ZM512, BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512DQ))
    // AVX512-FP16's half-precision elements: the unary operations under a mask (the 512-bit getexp with the exceptions'
    // suppression), and the conversions of each width, from a's bytes per lane into w's bytes where they aren't a's.
    BUSTER_CHECK_X86_HALF("getexpph128_mask", MASK_UNARY, 2, 16, VGETEXPPH_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("getexpph256_mask", MASK_UNARY, 2, 32, VGETEXPPH_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("getexpph512_mask", MASK_UNARY, 2, 64, VGETEXPPH_Z_ZM512, 0, VGETEXPPH_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_HALF("rcpph128_mask", MASK_UNARY, 2, 16, VRCPPH_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("rcpph256_mask", MASK_UNARY, 2, 32, VRCPPH_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF("rcpph512_mask", MASK_UNARY, 2, 64, VRCPPH_Z_ZM512, 0)
    BUSTER_CHECK_X86_HALF("rsqrtph128_mask", MASK_UNARY, 2, 16, VRSQRTPH_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("rsqrtph256_mask", MASK_UNARY, 2, 32, VRSQRTPH_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF("rsqrtph512_mask", MASK_UNARY, 2, 64, VRSQRTPH_Z_ZM512, 0)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2psx", VCVTPH2PSX, 4, 16, 0, X_XM64, 2, 16, 32, Y_XM128, 2, 32, 64, Z_YM256, Z_Y_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2pd", VCVTPH2PD, 8, 16, 0, X_XM32, 4, 16, 32, Y_XM64, 2, 16, 64, Z_XM128, Z_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2dq", VCVTPH2DQ, 4, 16, 0, X_XM64, 2, 16, 32, Y_XM128, 2, 32, 64, Z_YM256, Z_Y_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2udq", VCVTPH2UDQ, 4, 16, 0, X_XM64, 2, 16, 32, Y_XM128, 2, 32, 64, Z_YM256, Z_Y_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2dq", VCVTTPH2DQ, 4, 16, 0, X_XM64, 2, 16, 32, Y_XM128, 2, 32, 64, Z_YM256, Z_Y_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2udq", VCVTTPH2UDQ, 4, 16, 0, X_XM64, 2, 16, 32, Y_XM128, 2, 32, 64, Z_YM256, Z_Y_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2qq", VCVTPH2QQ, 8, 16, 0, X_XM32, 4, 16, 32, Y_XM64, 2, 16, 64, Z_XM128, Z_X_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2uqq", VCVTPH2UQQ, 8, 16, 0, X_XM32, 4, 16, 32, Y_XM64, 2, 16, 64, Z_XM128, Z_X_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2qq", VCVTTPH2QQ, 8, 16, 0, X_XM32, 4, 16, 32, Y_XM64, 2, 16, 64, Z_XM128, Z_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2uqq", VCVTTPH2UQQ, 8, 16, 0, X_XM32, 4, 16, 32, Y_XM64, 2, 16, 64, Z_XM128, Z_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2w", VCVTPH2W, 2, 16, 0, X_XM128, 2, 32, 0, Y_YM256, 2, 64, 0, Z_ZM512, Z_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtph2uw", VCVTPH2UW, 2, 16, 0, X_XM128, 2, 32, 0, Y_YM256, 2, 64, 0, Z_ZM512, Z_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2w", VCVTTPH2W, 2, 16, 0, X_XM128, 2, 32, 0, Y_YM256, 2, 64, 0, Z_ZM512, Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvttph2uw", VCVTTPH2UW, 2, 16, 0, X_XM128, 2, 32, 0, Y_YM256, 2, 64, 0, Z_ZM512, Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtps2phx", VCVTPS2PHX, 4, 16, 0, X_XM128, 4, 32, 16, X_YM256, 4, 64, 32, Y_ZM512, Y_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtpd2ph", VCVTPD2PH, 8, 16, 0, X_XM128, 8, 32, 16, X_YM256, 8, 64, 16, X_ZM512, X_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtqq2ph", VCVTQQ2PH, 8, 16, 0, X_XM128, 8, 32, 16, X_YM256, 8, 64, 16, X_ZM512, X_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_CONVERSIONS("vcvtuqq2ph", VCVTUQQ2PH, 8, 16, 0, X_XM128, 8, 32, 16, X_YM256, 8, 64, 16, X_ZM512, X_Z_ER, ER)
    // The ones clang 23's headers write at 128 and 512 bits alone, or at 512 (the others are __builtin_convertvector's).
    BUSTER_CHECK_X86_HALF("vcvtdq2ph128_mask", MASK_UNARY, 4, 16, VCVTDQ2PH_X_XM128, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtdq2ph512_mask", MASK_UNARY, 4, 64, VCVTDQ2PH_Y_ZM512, 32, VCVTDQ2PH_Y_Z_ER, ER)
    BUSTER_CHECK_X86_HALF("vcvtudq2ph128_mask", MASK_UNARY, 4, 16, VCVTUDQ2PH_X_XM128, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtudq2ph512_mask", MASK_UNARY, 4, 64, VCVTUDQ2PH_Y_ZM512, 32, VCVTUDQ2PH_Y_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtw2ph512_mask", MASK_UNARY, 2, 64, VCVTW2PH_Z_ZM512, 0, VCVTW2PH_Z_Z_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtuw2ph512_mask", MASK_UNARY, 2, 64, VCVTUW2PH_Z_ZM512, 0, VCVTUW2PH_Z_Z_ER, ER)
    // Two vectors' operation merged into w under the mask, the scalar ones' other elements a's, and their conversions'
    // of b's first element.
    BUSTER_CHECK_X86_HALF("scalefph128_mask", MASK_MERGE, 2, 16, VSCALEFPH_X_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("scalefph256_mask", MASK_MERGE, 2, 32, VSCALEFPH_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("scalefph512_mask", MASK_MERGE, 2, 64, VSCALEFPH_Z_Z_ZM512, 0, VSCALEFPH_Z_Z_Z_ER, ER)
    BUSTER_CHECK_X86_HALF("rcpsh_mask", MASK_MERGE, 2, 16, VRCPSH_X_X_XM16, 0)
    BUSTER_CHECK_X86_HALF("rsqrtsh_mask", MASK_MERGE, 2, 16, VRSQRTSH_X_X_XM16, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("getexpsh128_round_mask", MASK_MERGE, 2, 16, VGETEXPSH_X_X_XM16, 0, VGETEXPSH_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("maxsh_round_mask", MASK_MERGE, 2, 16, VMAXSH_X_X_XM16, 0, VMAXSH_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("minsh_round_mask", MASK_MERGE, 2, 16, VMINSH_X_X_XM16, 0, VMINSH_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("scalefsh_round_mask", MASK_MERGE, 2, 16, VSCALEFSH_X_X_XM16, 0, VSCALEFSH_X_X_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("sqrtsh_round_mask", MASK_MERGE, 2, 16, VSQRTSH_X_X_XM16, 0, VSQRTSH_X_X_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsd2sh_round_mask", MASK_MERGE, 2, 16, VCVTSD2SH_X_X_XM64, 0, VCVTSD2SH_X_X_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2sd_round_mask", MASK_MERGE, 8, 16, VCVTSH2SD_X_X_XM16, 0, VCVTSH2SD_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2ss_round_mask", MASK_MERGE, 4, 16, VCVTSH2SS_X_X_XM16, 0, VCVTSH2SS_X_X_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtss2sh_round_mask", MASK_MERGE, 2, 16, VCVTSS2SH_X_X_XM32, 0, VCVTSS2SH_X_X_X_ER, ER)
    // The complex products (a 32-bit lane each: two half-precision elements, the real one first), plain and conjugate,
    // added to c (FMADDC) or not (FMULC), into c's or w's register as the instructions take them, merged as clang's
    // code merges them (CheckX86LowerMerge).
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph128_mask", 16, VFMADDCPH_X_X_XM128, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph128_maskz", 16, VFMADDCPH_X_X_XM128, NONE, NONE, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph256_mask", 32, VFMADDCPH_Y_Y_YM256, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph256_maskz", 32, VFMADDCPH_Y_Y_YM256, NONE, NONE, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph512_mask", 64, VFMADDCPH_Z_Z_ZM512, VFMADDCPH_Z_Z_Z_ER, ER, A)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph512_mask3", 64, VFMADDCPH_Z_Z_ZM512, VFMADDCPH_Z_Z_Z_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcph512_maskz", 64, VFMADDCPH_Z_Z_ZM512, VFMADDCPH_Z_Z_Z_ER, ER, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcsh_mask", 16, VFMADDCSH_X_X_XM32, VFMADDCSH_X_X_X_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcsh_maskz", 16, VFMADDCSH_X_X_XM32, VFMADDCSH_X_X_X_ER, ER, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcsh_round_mask", 16, VFMADDCSH_X_X_XM32, VFMADDCSH_X_X_X_ER, ER, A_SCALAR)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmaddcsh_round_mask3", 16, VFMADDCSH_X_X_XM32, VFMADDCSH_X_X_X_ER, ER, C_SCALAR)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph128_mask", 16, VFCMADDCPH_X_X_XM128, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph128_maskz", 16, VFCMADDCPH_X_X_XM128, NONE, NONE, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph256_mask", 32, VFCMADDCPH_Y_Y_YM256, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph256_maskz", 32, VFCMADDCPH_Y_Y_YM256, NONE, NONE, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph512_mask", 64, VFCMADDCPH_Z_Z_ZM512, VFCMADDCPH_Z_Z_Z_ER, ER, A)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph512_mask3", 64, VFCMADDCPH_Z_Z_ZM512, VFCMADDCPH_Z_Z_Z_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcph512_maskz", 64, VFCMADDCPH_Z_Z_ZM512, VFCMADDCPH_Z_Z_Z_ER, ER, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcsh_mask", 16, VFCMADDCSH_X_X_XM32, VFCMADDCSH_X_X_X_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcsh_maskz", 16, VFCMADDCSH_X_X_XM32, VFCMADDCSH_X_X_X_ER, ER, W_ZEROED)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcsh_round_mask", 16, VFCMADDCSH_X_X_XM32, VFCMADDCSH_X_X_X_ER, ER, A_SCALAR)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmaddcsh_round_mask3", 16, VFCMADDCSH_X_X_XM32, VFCMADDCSH_X_X_X_ER, ER, C_SCALAR)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmulcph128_mask", 16, VFMULCPH_X_X_XM128, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmulcph256_mask", 32, VFMULCPH_Y_Y_YM256, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmulcph512_mask", 64, VFMULCPH_Z_Z_ZM512, VFMULCPH_Z_Z_Z_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfmulcsh_mask", 16, VFMULCSH_X_X_XM32, VFMULCSH_X_X_X_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmulcph128_mask", 16, VFCMULCPH_X_X_XM128, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmulcph256_mask", 32, VFCMULCPH_Y_Y_YM256, NONE, NONE, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmulcph512_mask", 64, VFCMULCPH_Z_Z_ZM512, VFCMULCPH_Z_Z_Z_ER, ER, W)
    BUSTER_CHECK_X86_HALF_COMPLEX("vfcmulcsh_mask", 16, VFCMULCSH_X_X_XM32, VFCMULCSH_X_X_X_ER, ER, W)
    // The FMAs under a mask at 64 bytes and of the scalars, as the floats' and doubles', and the alternating ones of 16
    // and 32 bytes without one.
    BUSTER_CHECK_X86_HALF_FMA("vfmaddph512_mask", 2, 64, VFMADD213PH_Z_Z_ZM512, VFMADD213PH_Z_Z_Z_ER, A)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddph512_maskz", 2, 64, VFMADD213PH_Z_Z_ZM512, VFMADD213PH_Z_Z_Z_ER, A_ZEROED)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddph512_mask3", 2, 64, VFMADD231PH_Z_Z_ZM512, VFMADD231PH_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_HALF_FMA("vfmsubph512_mask3", 2, 64, VFMSUB231PH_Z_Z_ZM512, VFMSUB231PH_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsubph512_mask", 2, 64, VFMADDSUB213PH_Z_Z_ZM512, VFMADDSUB213PH_Z_Z_Z_ER, A)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsubph512_maskz", 2, 64, VFMADDSUB213PH_Z_Z_ZM512, VFMADDSUB213PH_Z_Z_Z_ER, A_ZEROED)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsubph512_mask3", 2, 64, VFMADDSUB231PH_Z_Z_ZM512, VFMADDSUB231PH_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_HALF_FMA("vfmsubaddph512_mask3", 2, 64, VFMSUBADD231PH_Z_Z_ZM512, VFMSUBADD231PH_Z_Z_Z_ER, C)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsh3_mask", 2, 16, VFMADD213SH_X_X_XM16, VFMADD213SH_X_X_X_ER, A)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsh3_maskz", 2, 16, VFMADD213SH_X_X_XM16, VFMADD213SH_X_X_X_ER, A_ZEROED)
    BUSTER_CHECK_X86_HALF_FMA("vfmaddsh3_mask3", 2, 16, VFMADD231SH_X_X_XM16, VFMADD231SH_X_X_X_ER, C)
    BUSTER_CHECK_X86_HALF_FMA("vfmsubsh3_mask3", 2, 16, VFMSUB231SH_X_X_XM16, VFMSUB231SH_X_X_X_ER, C)
    BUSTER_CHECK_X86_HALF("vfmaddsubph", TERNARY, 2, 16, VFMADDSUB213PH_X_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("vfmaddsubph256", TERNARY, 2, 32, VFMADDSUB213PH_Y_Y_YM256, 0)
    // The others: max and min, the 512-bit ones with the exceptions' suppression; the scalar select, by VMOVSH; the
    // scalar's masked loads and stores, the whole vector's under the mask (VMOVDQU16's), as clang's are; the scalar
    // conversions to integers, by a rounding control or truncated; and the scalar compare into an int by its predicate.
    BUSTER_CHECK_X86_HALF("maxph128", BINARY, 2, 16, VMAXPH_X_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("maxph256", BINARY, 2, 32, VMAXPH_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("maxph512", BINARY, 2, 64, VMAXPH_Z_Z_ZM512, 0, VMAXPH_Z_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_HALF("minph128", BINARY, 2, 16, VMINPH_X_X_XM128, 0)
    BUSTER_CHECK_X86_HALF("minph256", BINARY, 2, 32, VMINPH_Y_Y_YM256, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("minph512", BINARY, 2, 64, VMINPH_Z_Z_ZM512, 0, VMINPH_Z_Z_Z_SAE, SAE)
    BUSTER_CHECK_X86_HALF("selectsh_128", SELECT_SCALAR, 2, 16, VMOVSH_X_X_X, 0)
    BUSTER_CHECK_X86_HALF("loadsh128_mask", LOAD, 2, 16, NONE, 0)
    BUSTER_CHECK_X86_HALF("storesh128_mask", STORE, 2, 16, NONE, 0)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2si32", CONVERT, 2, 16, VCVTSH2SI_R32_XM16, 4, VCVTSH2SI_R32_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2si64", CONVERT, 2, 16, VCVTSH2SI_R64_XM16, 8, VCVTSH2SI_R64_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2usi32", CONVERT, 2, 16, VCVTSH2USI_R32_XM16, 4, VCVTSH2USI_R32_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvtsh2usi64", CONVERT, 2, 16, VCVTSH2USI_R64_XM16, 8, VCVTSH2USI_R64_X_ER, ER)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvttsh2si32", CONVERT, 2, 16, VCVTTSH2SI_R32_XM16, 4, VCVTTSH2SI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvttsh2si64", CONVERT, 2, 16, VCVTTSH2SI_R64_XM16, 8, VCVTTSH2SI_R64_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvttsh2usi32", CONVERT, 2, 16, VCVTTSH2USI_R32_XM16, 4, VCVTTSH2USI_R32_X_SAE, SAE)
    BUSTER_CHECK_X86_HALF_ROUNDED("vcvttsh2usi64", CONVERT, 2, 16, VCVTTSH2USI_R64_XM16, 8, VCVTTSH2USI_R64_X_SAE, SAE)
    BUSTER_CBUILTIN_X86_ROW("__builtin_ia32_vcomish", sizeof("__builtin_ia32_vcomish") - 1, CHECK_X86_LOWER_SCALAR_COMPARE, 2, 16, false, false, 2, X86_FORM_VCMPSH_K_X_XM16_IMM8, 0,
      false, 0, X86_FORM_VCMPSH_K_X_X_SAE_IMM8, CHECK_X86_ROUND_SAE, 0, BUSTER_CHECK_X86_FP16)
    // The reductions of half-precision elements at each width, as the floats' of 64 bytes.
    BUSTER_CHECK_X86_HALF_REDUCE("fadd_ph128", 16, ADD)
    BUSTER_CHECK_X86_HALF_REDUCE("fadd_ph256", 32, ADD)
    BUSTER_CHECK_X86_HALF_REDUCE("fadd_ph512", 64, ADD)
    BUSTER_CHECK_X86_HALF_REDUCE("fmul_ph128", 16, MUL)
    BUSTER_CHECK_X86_HALF_REDUCE("fmul_ph256", 32, MUL)
    BUSTER_CHECK_X86_HALF_REDUCE("fmul_ph512", 64, MUL)
    BUSTER_CHECK_X86_HALF_REDUCE("fmax_ph128", 16, MAX)
    BUSTER_CHECK_X86_HALF_REDUCE("fmax_ph256", 32, MAX)
    BUSTER_CHECK_X86_HALF_REDUCE("fmax_ph512", 64, MAX)
    BUSTER_CHECK_X86_HALF_REDUCE("fmin_ph128", 16, MIN)
    BUSTER_CHECK_X86_HALF_REDUCE("fmin_ph256", 32, MIN)
    BUSTER_CHECK_X86_HALF_REDUCE("fmin_ph512", 64, MIN)
    BUSTER_CHECK_X86_BF16_MEMORY("loadsbf16128_mask", LOAD)
    BUSTER_CHECK_X86_BF16_MEMORY("storesbf16128_mask", STORE)
    // XOP's compares.
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomb", 1, true, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomw", 2, true, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomd", 4, true, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomq", 8, true, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomub", 1, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomuw", 2, false, BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomud", 4, false, BUSTER_X86_FEATURE(AVX512DQ))
    BUSTER_CHECK_X86_XOP_COMPARE("vpcomuq", 8, false, BUSTER_X86_FEATURE(AVX512DQ))
    // XOP's own: FRCZ's fractions, the horizontal sums and differences of adjacent elements into wider ones, the shifts by
    // a vector of counts (the source in ModRM.rm and the counts in VEX.vvvv, W0's forms, as LLVM emits them: `swap`), the
    // multiply-accumulates of a and b into c and VPPERM's bytes of a and b by c (c the /is4 register, BLENDV's).
    BUSTER_CHECK_X86_XOP("vfrczps", UNARY, 4, 16, false, VFRCZPS_X_XM128)
    BUSTER_CHECK_X86_XOP("vfrczps256", UNARY, 4, 32, false, VFRCZPS_Y_YM256)
    BUSTER_CHECK_X86_XOP("vfrczpd", UNARY, 8, 16, false, VFRCZPD_X_XM128)
    BUSTER_CHECK_X86_XOP("vfrczpd256", UNARY, 8, 32, false, VFRCZPD_Y_YM256)
    BUSTER_CHECK_X86_XOP("vfrczss", UNARY, 4, 16, false, VFRCZSS_X_XM32)
    BUSTER_CHECK_X86_XOP("vfrczsd", UNARY, 8, 16, false, VFRCZSD_X_XM64)
    BUSTER_CHECK_X86_XOP("vphaddbw", UNARY, 1, 16, false, VPHADDBW_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddbd", UNARY, 1, 16, false, VPHADDBD_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddbq", UNARY, 1, 16, false, VPHADDBQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddwd", UNARY, 2, 16, false, VPHADDWD_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddwq", UNARY, 2, 16, false, VPHADDWQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphadddq", UNARY, 4, 16, false, VPHADDDQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddubw", UNARY, 1, 16, false, VPHADDUBW_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddubd", UNARY, 1, 16, false, VPHADDUBD_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddubq", UNARY, 1, 16, false, VPHADDUBQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphadduwd", UNARY, 2, 16, false, VPHADDUWD_X_XM128)
    BUSTER_CHECK_X86_XOP("vphadduwq", UNARY, 2, 16, false, VPHADDUWQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphaddudq", UNARY, 4, 16, false, VPHADDUDQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vphsubbw", UNARY, 1, 16, false, VPHSUBBW_X_XM128)
    BUSTER_CHECK_X86_XOP("vphsubwd", UNARY, 2, 16, false, VPHSUBWD_X_XM128)
    BUSTER_CHECK_X86_XOP("vphsubdq", UNARY, 4, 16, false, VPHSUBDQ_X_XM128)
    BUSTER_CHECK_X86_XOP("vpshab", BINARY, 1, 16, true, VPSHAB_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshaw", BINARY, 2, 16, true, VPSHAW_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshad", BINARY, 4, 16, true, VPSHAD_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshaq", BINARY, 8, 16, true, VPSHAQ_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshlb", BINARY, 1, 16, true, VPSHLB_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshlw", BINARY, 2, 16, true, VPSHLW_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshld", BINARY, 4, 16, true, VPSHLD_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpshlq", BINARY, 8, 16, true, VPSHLQ_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacssww", BLENDV, 2, 16, false, VPMACSSWW_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacsswd", BLENDV, 2, 16, false, VPMACSSWD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacssdql", BLENDV, 4, 16, false, VPMACSSDQL_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacssdd", BLENDV, 4, 16, false, VPMACSSDD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacssdqh", BLENDV, 4, 16, false, VPMACSSDQH_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacsww", BLENDV, 2, 16, false, VPMACSWW_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacswd", BLENDV, 2, 16, false, VPMACSWD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacsdql", BLENDV, 4, 16, false, VPMACSDQL_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacsdd", BLENDV, 4, 16, false, VPMACSDD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmacsdqh", BLENDV, 4, 16, false, VPMACSDQH_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmadcsswd", BLENDV, 2, 16, false, VPMADCSSWD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpmadcswd", BLENDV, 2, 16, false, VPMADCSWD_X_X_XM128_X)
    BUSTER_CHECK_X86_XOP("vpperm", BLENDV, 1, 16, false, VPPERM_X_X_XM128_X)
    // AVX-NE-CONVERT's loads, of 16 and 32 bytes of floats.
    BUSTER_CHECK_X86_NE_CONVERT("vbcstnebf162ps128", 16, VBCSTNEBF162PS_X_M16)
    BUSTER_CHECK_X86_NE_CONVERT("vbcstnebf162ps256", 32, VBCSTNEBF162PS_Y_M16)
    BUSTER_CHECK_X86_NE_CONVERT("vbcstnesh2ps128", 16, VBCSTNESH2PS_X_M16)
    BUSTER_CHECK_X86_NE_CONVERT("vbcstnesh2ps256", 32, VBCSTNESH2PS_Y_M16)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneebf162ps128", 16, VCVTNEEBF162PS_X_M128)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneebf162ps256", 32, VCVTNEEBF162PS_Y_M256)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneeph2ps128", 16, VCVTNEEPH2PS_X_M128)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneeph2ps256", 32, VCVTNEEPH2PS_Y_M256)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneobf162ps128", 16, VCVTNEOBF162PS_X_M128)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneobf162ps256", 32, VCVTNEOBF162PS_Y_M256)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneoph2ps128", 16, VCVTNEOPH2PS_X_M128)
    BUSTER_CHECK_X86_NE_CONVERT("vcvtneoph2ps256", 32, VCVTNEOPH2PS_Y_M256)
    // The dot products of signed and unsigned bytes (b) and words (w) into doublewords, the s ones with signed saturation:
    // the first operand's signedness, then the second's (su, signed by unsigned).
    BUSTER_CHECK_X86_VNNI_INT("vpdpbssd", VPDPBSSD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpbssds", VPDPBSSDS)
    BUSTER_CHECK_X86_VNNI_INT("vpdpbsud", VPDPBSUD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpbsuds", VPDPBSUDS)
    BUSTER_CHECK_X86_VNNI_INT("vpdpbuud", VPDPBUUD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpbuuds", VPDPBUUDS)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwsud", VPDPWSUD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwsuds", VPDPWSUDS)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwusd", VPDPWUSD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwusds", VPDPWUSDS)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwuud", VPDPWUUD)
    BUSTER_CHECK_X86_VNNI_INT("vpdpwuuds", VPDPWUUDS)
    // SHA512's message steps (BINARY's, of forms without vvvv: a the destination too, the first one's b of 16 bytes) and
    // its two rounds (TERNARY's: a, b and c of 16 bytes), of 32 bytes; SM3's message steps (TERNARY's); and SM4's key
    // expansion and rounds of 64 bytes (BINARY's, AVX10.2's EVEX forms).
    BUSTER_CHECK_X86_EXTENDED("vsha512msg1", BINARY, 8, 32, VSHA512MSG1_Y_X, 16, BUSTER_X86_EXTENSION(SHA512))
    BUSTER_CHECK_X86_EXTENDED("vsha512msg2", BINARY, 8, 32, VSHA512MSG2_Y_Y, 0, BUSTER_X86_EXTENSION(SHA512))
    BUSTER_CHECK_X86_EXTENDED("vsha512rnds2", TERNARY, 8, 32, VSHA512RNDS2_Y_Y_X, 16, BUSTER_X86_EXTENSION(SHA512))
    BUSTER_CHECK_X86_EXTENDED("vsm3msg1", TERNARY, 4, 16, VSM3MSG1_X_X_XM128, 0, BUSTER_X86_EXTENSION(SM3))
    BUSTER_CHECK_X86_EXTENDED("vsm3msg2", TERNARY, 4, 16, VSM3MSG2_X_X_XM128, 0, BUSTER_X86_EXTENSION(SM3))
    BUSTER_CHECK_X86_EXTENDED("vsm4key4512", BINARY, 4, 64, VSM4KEY4_Z_Z_ZM512, 0, BUSTER_X86_EXTENSION(SM4) | BUSTER_X86_EXTENSION(AVX10_2))
    BUSTER_CHECK_X86_EXTENDED("vsm4rnds4512", BINARY, 4, 64, VSM4RNDS4_Z_Z_ZM512, 0, BUSTER_X86_EXTENSION(SM4) | BUSTER_X86_EXTENSION(AVX10_2))

#undef BUSTER_CHECK_X86_BF16_MEMORY
#undef BUSTER_CHECK_X86_NE_CONVERT
#undef BUSTER_CHECK_X86_VNNI_INT
#undef BUSTER_CHECK_X86_EXTENDED
#undef BUSTER_CHECK_X86_XOP_COMPARE
#undef BUSTER_CHECK_X86_XOP
#undef BUSTER_CHECK_X86_HALF_REDUCE
#undef BUSTER_CHECK_X86_HALF_CONVERSIONS
#undef BUSTER_CHECK_X86_HALF_COMPLEX
#undef BUSTER_CHECK_X86_HALF_FMA
#undef BUSTER_CHECK_X86_HALF_ROUNDED
#undef BUSTER_CHECK_X86_HALF
#undef BUSTER_CHECK_X86_FP16

#undef BUSTER_CHECK_X86_ROUNDED_IMMEDIATE
#undef BUSTER_CHECK_X86_REDUCE
#undef BUSTER_CHECK_X86_FMA
#undef BUSTER_CHECK_X86_MEMORY
#undef BUSTER_CHECK_X86_SYSTEM
#undef BUSTER_CHECK_X86_COMPARISONS
#undef BUSTER_CHECK_X86_TEST
#undef BUSTER_CHECK_X86_SHIFTS
#undef BUSTER_CHECK_X86_SUFFIXED
#undef BUSTER_CHECK_X86_RULE_OTHER
#undef BUSTER_CHECK_X86_RULE_MASK
#undef BUSTER_CHECK_X86_BINARIES
#undef BUSTER_CHECK_X86_FORMS
#undef BUSTER_CHECK_X86_ELEMENTS
#undef BUSTER_CHECK_X86_PREDICATES
#undef BUSTER_CHECK_X86_SSE
#undef BUSTER_CHECK_X86_RULE_FIXED
#undef BUSTER_CHECK_X86_ROUNDED
#undef BUSTER_CHECK_X86_ONE_OTHER
#undef BUSTER_CHECK_X86_ONE
#undef BUSTER_CHECK_X86_TRUNCATIONS
#undef BUSTER_CHECK_X86_TRUNCATION_STORES
#undef BUSTER_CHECK_X86_TRUNCATION
#undef BUSTER_CHECK_X86_WIDTHS
#undef BUSTER_CHECK_X86_RULE
