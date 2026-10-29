// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The intrinsics' x86 builtins as Tier 0's machine operations (#136, #148): the row of an X86 entry (Tier0Operation) for a
// builtin's call, its arguments converted to its prototype's types already (c_check_builtin_x86.c) and its immediates
// constants, as clang -O0's code computes it. A builtin's row is its family's with the rule's form, from a table by
// name; the families are those this compiler's own main.c reaches first. Each form is a VEX or EVEX one that names its
// operands in its fields alone, of AVX-512F and AVX-512VL, which the lowering requires of a function with vectors, and
// of the features the rule names past them. A builtin the table doesn't name is refused by name. One whose instruction
// names fixed registers (rdtsc, xgetbv, monitorx and mwaitx) or has a legacy encoding (pause, tzcnt), which no row takes,
// is a call of a per-unit leaf instead (c_builtin_x86_helpers.c). Included by c_check_program_lower.c after
// c_check_program_vectors.c.

typedef enum CheckX86LowerFamily
{
    // (vector a, vector b, int predicate, mask m): VPCMP's or VPCMPU's opmask of a and b under m.
    CHECK_X86_LOWER_COMPARE,
    // (mask m, vector w, vector a): w's elements where m's bits are set, a's elsewhere.
    CHECK_X86_LOWER_SELECT,
    // (mask m): m, through an opmask register.
    CHECK_X86_LOWER_KMOV,
    // (const element* p, vector w, mask m): p's elements where m's bits are set, w's elsewhere; p needs no alignment.
    CHECK_X86_LOWER_LOAD,
    // (element* p, vector a, mask m): a's elements stored where m's bits are set, no others touched.
    CHECK_X86_LOWER_STORE,
    // (vector a, vector b): the form's dest, a, b (reg, vvvv, rm), or with `swap` dest, b, a.
    CHECK_X86_LOWER_BINARY,
    // (vector a, int count): a shifted by the count, moved into an XMM register zero-extended (the form's count operand,
    // whose low 64 bits it reads: a count past the elements' bits gives 0, or the sign, as clang's code does).
    CHECK_X86_LOWER_SHIFT,
    // (vector a, int index): the element at the index, an immediate, in a general register.
    CHECK_X86_LOWER_EXTRACT,
    // (vector a, int value, int index): a with the element at the index, an immediate, the value.
    CHECK_X86_LOWER_INSERT,
    // (vector a): the elements' top bits, in a general register.
    CHECK_X86_LOWER_MOVEMASK,
    // (integer a, integer b): BMI2's dest, a, b: PDEP's and PEXT's source in vvvv and mask in rm, or with `swap` BZHI's
    // source in rm and index in vvvv.
    CHECK_X86_LOWER_GENERAL,
    // (): an undefined vector, which a row gives as zeros.
    CHECK_X86_LOWER_UNDEFINED,
    // (mask a, mask b): the form's k1, k1, k2 of a in k1 and b in k2 (KAND, KOR, KXOR, KADD, KUNPCK: vvvv's a is the
    // high half), or with `zero_test` whether that is 0 (ktestz's, which the caller tests: a row can't take its flag).
    CHECK_X86_LOWER_KBINARY,
    // (mask a, int count): a shifted by the count, an immediate (KSHIFTL, which gives 0 past the mask's bits).
    CHECK_X86_LOWER_KSHIFT,
    // (vector a, vector w, mask m): VPCOMPRESS's (or with `swap` VPEXPAND's) of a under m, merged into w.
    CHECK_X86_LOWER_COMPRESS,
    // (element* p, vector a, mask m): a's elements under m, stored one after another at p.
    CHECK_X86_LOWER_COMPRESS_STORE,
    // (vector a): VPMOVB2M's mask of a's elements' top bits.
    CHECK_X86_LOWER_VECTOR_MASK,
    // (mask m): VPMOVM2B's vector of -1 or 0 by m's bits.
    CHECK_X86_LOWER_MASK_VECTOR,
    // (vector a): the form's integer of a's low element, a truncating conversion (VCVTTSD2SI, VCVTTSS2SI).
    CHECK_X86_LOWER_CONVERT,
    // (vector a, int index, vector w, mask m): the form's lane of a at the index, an immediate, merged into w under m
    // (VEXTRACTI32X4, VEXTRACTI64X4), of `other` bytes.
    CHECK_X86_LOWER_EXTRACT_LANE,
    // (vector a, vector b, int immediate): the form's dest, a, b, imm (VALIGN, VSHUFI, VINSERTI, VGF2P8AFFINEQB), b of
    // `other` bytes where it isn't a's.
    CHECK_X86_LOWER_BINARY_IMMEDIATE,
    // (vector a, int immediate): the form's dest, a, imm: with `swap`, the destination in vvvv (VPROLD, VPSLLDQ).
    CHECK_X86_LOWER_UNARY_IMMEDIATE,
    // (vector a): the form's dest, a (VPCONFLICTD).
    CHECK_X86_LOWER_UNARY,
    // (vector a, vector b, vector c[, int immediate][, mask m]): the form's a (the destination too), b, c: VPERMT2's,
    // VPDPBUSD's, and VPTERNLOG's with its immediate, under m merged into a.
    CHECK_X86_LOWER_TERNARY,
    // (vector a, vector w, mask m): VPMOV's truncation of a's elements under m, merged into w, of `other` bytes.
    CHECK_X86_LOWER_TRUNCATE,
    // (vector w, const void* p, vector index, mask m, int scale): VPGATHER's elements at p + index * scale under m,
    // merged into w; the index of `other` bytes.
    CHECK_X86_LOWER_GATHER,
    // (void* p, mask m, vector index, vector a, int scale): VPSCATTER's of a's elements to p + index * scale under m; a
    // of `other` bytes.
    CHECK_X86_LOWER_SCATTER,
    // (vector a, vector b, vector m): VPBLENDVB's b's bytes where m's have their top bits, a's elsewhere.
    CHECK_X86_LOWER_BLENDV,
    // (vector a, vector b, mask m): VPSHUFBITQMB's mask under m.
    CHECK_X86_LOWER_MASK_BINARY,
    // (element* p, vector a, mask m): VPMOV's truncation of a's elements, each stored at its place at p where m's bit is
    // set (the form's memory destination); a's lanes are the mask's.
    CHECK_X86_LOWER_TRUNCATE_STORE,
} CheckX86LowerFamily;

#define BUSTER_CHECK_X86_NO_IMMEDIATE 0xffu

typedef struct CheckX86LowerRule CheckX86LowerRule;
struct CheckX86LowerRule
{
    const char* name;
    uint8_t name_length;
    uint8_t family;
    // The elements' bytes (1, 2, 4 or 8) and the vector's (16, 32 or 64), or the integers' (4 or 8) for GENERAL.
    uint8_t element;
    uint8_t bytes;
    bool is_signed;
    bool swap;
    // The immediate argument's index, or BUSTER_CHECK_X86_NO_IMMEDIATE.
    uint8_t immediate;
    // The family's form: but COMPARE's, SELECT's, KMOV's, LOAD's, STORE's and UNDEFINED's, which their elements pick.
    uint16_t form;
    // Another vector's bytes, where the family has one of other bytes than the first (see the families), or 0.
    uint8_t other;
    // KBINARY's: whether the result is that the form's is 0.
    bool zero_test;
    // Its features past AVX-512F and AVX-512VL.
    uint64_t features;
};

#define BUSTER_CHECK_X86_BW BUSTER_X86_FEATURE(AVX512BW)
#define BUSTER_CHECK_X86_RULE(name, family, element, bytes, is_signed, swap, immediate, form, features) \
    { name, sizeof(name) - 1, family, element, bytes, is_signed, swap, immediate, form, 0, false, features },
// A rule with another vector's bytes, or KBINARY's zero test.
#define BUSTER_CHECK_X86_RULE_OTHER(name, family, element, bytes, swap, immediate, form, other, features) \
    { name, sizeof(name) - 1, family, element, bytes, false, swap, immediate, form, other, false, features },
#define BUSTER_CHECK_X86_RULE_MASK(name, family, bytes, form, zero_test, features) \
    { name, sizeof(name) - 1, family, 1, bytes, false, false, family == CHECK_X86_LOWER_KSHIFT ? 1 : BUSTER_CHECK_X86_NO_IMMEDIATE, form, 0, zero_test, features },
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

static const CheckX86LowerRule check_x86_lower_rules[] = {
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_cmp", "b", "w", "d", "q", "_mask", CHECK_X86_LOWER_COMPARE, true, 2)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_ucmp", "b", "w", "d", "q", "_mask", CHECK_X86_LOWER_COMPARE, false, 2)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_select", "b_", "w_", "d_", "q_", "", CHECK_X86_LOWER_SELECT, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_loaddqu", "qi", "hi", "si", "di", "_mask", CHECK_X86_LOWER_LOAD, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
    BUSTER_CHECK_X86_ELEMENTS("__builtin_ia32_storedqu", "qi", "hi", "si", "di", "_mask", CHECK_X86_LOWER_STORE, false, BUSTER_CHECK_X86_NO_IMMEDIATE)
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
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_vpdpbusd512", CHECK_X86_LOWER_TERNARY, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPDPBUSD_Z_Z_ZM512, 0,
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
    // Truncations under a mask: `other` is the result's bytes, and its passthrough's.
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovdb512_mask", CHECK_X86_LOWER_TRUNCATE, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVDB_XM128_Z, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovdw512_mask", CHECK_X86_LOWER_TRUNCATE, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVDW_YM256_Z, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqb512_mask", CHECK_X86_LOWER_TRUNCATE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQB_XM64_Z, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqd512_mask", CHECK_X86_LOWER_TRUNCATE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQD_YM256_Z, 32, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqw512_mask", CHECK_X86_LOWER_TRUNCATE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQW_XM128_Z, 16, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovdb512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVDB_XM128_Z, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovdw512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 4, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVDW_YM256_Z, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqb512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQB_XM64_Z, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqd512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQD_YM256_Z, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovqw512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 8, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVQW_XM128_Z, 0, 0)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovwb512mem_mask", CHECK_X86_LOWER_TRUNCATE_STORE, 2, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVWB_YM256_Z, 0,
                                BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovwb512_mask", CHECK_X86_LOWER_TRUNCATE, 2, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVWB_YM256_Z, 32,
                                BUSTER_CHECK_X86_BW)
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_pmovuswb512_mask", CHECK_X86_LOWER_TRUNCATE, 2, 64, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_VPMOVUSWB_YM256_Z, 32,
                                BUSTER_CHECK_X86_BW)
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
};

#undef BUSTER_CHECK_X86_SHIFTS
#undef BUSTER_CHECK_X86_SUFFIXED
#undef BUSTER_CHECK_X86_RULE_OTHER
#undef BUSTER_CHECK_X86_RULE_MASK
#undef BUSTER_CHECK_X86_BINARIES
#undef BUSTER_CHECK_X86_FORMS
#undef BUSTER_CHECK_X86_ELEMENTS
#undef BUSTER_CHECK_X86_WIDTHS
#undef BUSTER_CHECK_X86_RULE

static const CheckX86LowerRule* check_x86_lower_rule(const char* name, uint32_t length)
{
    const CheckX86LowerRule* rule = NULL;
    for (uint32_t i = 0; rule == NULL && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        if (check_x86_lower_rules[i].name_length == length && memcmp(check_x86_lower_rules[i].name, name, length) == 0)
        {
            rule = check_x86_lower_rules + i;
        }
    }
    return rule;
}

// A mask of `bits` bits: its class, and the opmask moves to and from a general register (KMOVW for 8 and 16 bits, which
// moves 16, the value's high bits zero either way; KMOVD and KMOVQ, AVX-512BW's, for 32 and 64).
static uint8_t check_x86_lower_mask_class(uint32_t bits)
{
    return bits <= 8 ? BUSTER_TYPED_CLASS_U8 : bits == 16 ? BUSTER_TYPED_CLASS_U16 : bits == 32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
}

static uint16_t check_x86_lower_mask_in(uint32_t bits)
{
    return bits <= 16 ? X86_FORM_KMOVW_K_R32 : bits == 32 ? X86_FORM_KMOVD_K_R32 : X86_FORM_KMOVQ_K_R64;
}

static uint16_t check_x86_lower_mask_out(uint32_t bits)
{
    return bits <= 16 ? X86_FORM_KMOVW_R32_K : bits == 32 ? X86_FORM_KMOVD_R32_K : X86_FORM_KMOVQ_R64_K;
}

// The masked moves of an element's size at each width: VMOVDQU8, 16, 32 and 64, loads (to a register) or stores.
static uint16_t check_x86_lower_move(uint32_t element, uint32_t width, bool store)
{
    static const uint16_t loads[4][3] = {
        { X86_FORM_VMOVDQU8_X_XM128, X86_FORM_VMOVDQU8_Y_YM256, X86_FORM_VMOVDQU8_Z_ZM512 },
        { X86_FORM_VMOVDQU16_X_XM128, X86_FORM_VMOVDQU16_Y_YM256, X86_FORM_VMOVDQU16_Z_ZM512 },
        { X86_FORM_VMOVDQU32_X_XM128, X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_Z_ZM512 },
        { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 },
    };
    static const uint16_t stores[4][3] = {
        { X86_FORM_VMOVDQU8_XM128_X, X86_FORM_VMOVDQU8_YM256_Y, X86_FORM_VMOVDQU8_ZM512_Z },
        { X86_FORM_VMOVDQU16_XM128_X, X86_FORM_VMOVDQU16_YM256_Y, X86_FORM_VMOVDQU16_ZM512_Z },
        { X86_FORM_VMOVDQU32_XM128_X, X86_FORM_VMOVDQU32_YM256_Y, X86_FORM_VMOVDQU32_ZM512_Z },
        { X86_FORM_VMOVDQU64_XM128_X, X86_FORM_VMOVDQU64_YM256_Y, X86_FORM_VMOVDQU64_ZM512_Z },
    };
    const uint32_t size = element == 1 ? 0 : element == 2 ? 1 : element == 4 ? 2 : 3;
    return store ? stores[size][width] : loads[size][width];
}

static X86Instruction check_x86_lower_record(uint32_t form, uint8_t reg, uint8_t vvvv, uint8_t rm)
{
    return (X86Instruction){ .form = (uint16_t)form, .reg = reg, .vvvv = vvvv, .rm = rm, .mode = BUSTER_X86_REGISTER_DIRECT };
}

// The index of a builtin's immediate argument, BUSTER_CHECK_X86_NO_IMMEDIATE for none, or for one the table doesn't name.
static uint32_t check_x86_lower_immediate(const char* name, uint32_t length)
{
    const CheckX86LowerRule* rule = check_x86_lower_rule(name, length);
    return rule != NULL ? rule->immediate : BUSTER_CHECK_X86_NO_IMMEDIATE;
}

// Whether a builtin's result is that its row's is 0 (ktestz's), which the caller tests.
static bool check_x86_lower_zero_test(const char* name, uint32_t length)
{
    const CheckX86LowerRule* rule = check_x86_lower_rule(name, length);
    return rule != NULL && rule->zero_test;
}

// The vector moves of a width (16, 32 or 64 bytes) into VECTOR(0): VMOVDQU64's, which a row ends with where its
// instruction leaves its result in another register.
static X86Instruction check_x86_lower_move_result(uint32_t bytes, uint8_t from)
{
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    return check_x86_lower_record(check_x86_lower_move(8, width, false), BUSTER_TIER0_ROLE_VECTOR(0), 0, from);
}

// Whether a family moves a mask between a general register and an opmask register, which for 32 lanes and more takes
// AVX-512BW's KMOVD or KMOVQ.
static bool check_x86_lower_family_masks(uint8_t family)
{
    bool result = false;
    switch (family)
    {
    case CHECK_X86_LOWER_COMPARE:
    case CHECK_X86_LOWER_SELECT:
    case CHECK_X86_LOWER_KMOV:
    case CHECK_X86_LOWER_LOAD:
    case CHECK_X86_LOWER_STORE:
    case CHECK_X86_LOWER_KBINARY:
    case CHECK_X86_LOWER_KSHIFT:
    case CHECK_X86_LOWER_COMPRESS:
    case CHECK_X86_LOWER_COMPRESS_STORE:
    case CHECK_X86_LOWER_VECTOR_MASK:
    case CHECK_X86_LOWER_MASK_VECTOR:
    case CHECK_X86_LOWER_EXTRACT_LANE:
    case CHECK_X86_LOWER_TERNARY:
    case CHECK_X86_LOWER_TRUNCATE:
    case CHECK_X86_LOWER_GATHER:
    case CHECK_X86_LOWER_SCATTER:
    case CHECK_X86_LOWER_MASK_BINARY:
    case CHECK_X86_LOWER_TRUNCATE_STORE: result = true; break;
    default: break;
    }
    return result;
}

// The row of an x86 builtin's call on a target: its records, operands and result, or the reason it has none.
// `immediate` is its immediate argument's constant value (check_x86_lower_immediate's), which a record takes and no
// operand is. Operands are the other arguments in order, and a role's j is its operand's place among them all, so that
// a compare's mask is INTEGER(2) and a select's vectors VECTOR(1) and VECTOR(2). An integer operand's class here is its
// width's; the caller gives each its argument's own (check_lower_x86_builtin). A result is in the class the instruction
// extends it to, which the caller converts to the builtin's type.
static const char* check_x86_lower_row(uint64_t target, const char* name, uint32_t length, uint64_t immediate, Tier0Operation* row)
{
    const CheckX86LowerRule* rule = check_x86_lower_rule(name, length);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    if (rule == NULL)
    {
        reason = "this x86 builtin";
    }
    else
    {
        const uint32_t width = rule->bytes == 16 ? 0 : rule->bytes == 32 ? 1 : 2;
        const bool mask_bits = rule->family == CHECK_X86_LOWER_KMOV || rule->family == CHECK_X86_LOWER_KBINARY || rule->family == CHECK_X86_LOWER_KSHIFT;
        const uint32_t lanes = mask_bits ? 8u * rule->bytes : rule->family == CHECK_X86_LOWER_EXTRACT_LANE ? (uint32_t)rule->other / rule->element :
                                                                                                          (uint32_t)rule->bytes / rule->element;
        const uint8_t mask_class = check_x86_lower_mask_class(lanes);
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
        const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
        const bool masked = rule->family <= CHECK_X86_LOWER_STORE;
        // Integer-only opmask rows never reach the VECTOR value-class gate. Their KMOV and mask instructions still
        // require AVX-512F; keep GENERAL's BMI2-only target independent of that requirement.
        needed = rule->features | (mask_bits ? BUSTER_X86_FEATURE(AVX512F) : 0) |
                 (masked && rule->element <= 2 && rule->family != CHECK_X86_LOWER_KMOV ? BUSTER_CHECK_X86_BW : 0) |
                 (check_x86_lower_family_masks(rule->family) && lanes >= 32 ? BUSTER_CHECK_X86_BW : 0);
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = rule->bytes;
        if (rule->family == CHECK_X86_LOWER_COMPARE)
        {
            static const uint16_t signed_forms[4][3] = {
                { X86_FORM_VPCMPB_K_X_XM128_IMM8, X86_FORM_VPCMPB_K_Y_YM256_IMM8, X86_FORM_VPCMPB_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPW_K_X_XM128_IMM8, X86_FORM_VPCMPW_K_Y_YM256_IMM8, X86_FORM_VPCMPW_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPD_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPQ_K_X_XM128_IMM8, X86_FORM_VPCMPQ_K_Y_YM256_IMM8, X86_FORM_VPCMPQ_K_Z_ZM512_IMM8 },
            };
            static const uint16_t unsigned_forms[4][3] = {
                { X86_FORM_VPCMPUB_K_X_XM128_IMM8, X86_FORM_VPCMPUB_K_Y_YM256_IMM8, X86_FORM_VPCMPUB_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPUW_K_X_XM128_IMM8, X86_FORM_VPCMPUW_K_Y_YM256_IMM8, X86_FORM_VPCMPUW_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPUD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 },
                { X86_FORM_VPCMPUQ_K_X_XM128_IMM8, X86_FORM_VPCMPUQ_K_Y_YM256_IMM8, X86_FORM_VPCMPUQ_K_Z_ZM512_IMM8 },
            };
            const uint32_t size = rule->element == 1 ? 0 : rule->element == 2 ? 1 : rule->element == 4 ? 2 : 3;
            // The predicate is the immediate's low 3 bits, as the instruction reads them.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 2, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = check_x86_lower_record(rule->is_signed ? signed_forms[size][width] : unsigned_forms[size][width], 1, v0, v1);
            row->records[1].immediate = (int64_t)(immediate & 7);
            row->records[1].decorations = 2;
            row->records[2] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_SELECT)
        {
            // A's elements, then W's merged in where the mask's bits are set.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v2);
            row->records[2] = check_x86_lower_record(check_x86_lower_move(rule->element, width, false), v0, 0, v1);
            row->records[2].decorations = 1;
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_KMOV)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_LOAD || rule->family == CHECK_X86_LOWER_STORE)
        {
            // The load merges into W's register and leaves it in VECTOR(0); the store writes A's elements alone.
            const bool store = rule->family == CHECK_X86_LOWER_STORE;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = (X86Instruction){ .form = check_x86_lower_move(rule->element, width, store), .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0),
                                                .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 };
            row->record_count = 2;
            if (!store)
            {
                row->records[2] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v1);
                row->record_count = 3;
            }
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = store ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_VECTOR;
            row->result_bytes = store ? 0 : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_BINARY)
        {
            row->records[0] = check_x86_lower_record(rule->form, v0, rule->swap ? v1 : v0, rule->swap ? v0 : v1);
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_SHIFT)
        {
            row->records[0] = check_x86_lower_record(X86_FORM_VMOVD_X_RM32_EVEX, v1, 0, BUSTER_TIER0_ROLE_INTEGER(1));
            row->records[1] = check_x86_lower_record(rule->form, v0, v0, v1);
            row->record_count = 2;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_I32;
        }
        else if (rule->family == CHECK_X86_LOWER_EXTRACT)
        {
            // The element at the index's low bits, zero-extended into the general register (but VPEXTRQ's, whole); of a
            // 256-bit vector, from its high lane extracted first where the index is in it.
            const uint64_t count = (uint64_t)rule->bytes / rule->element;
            const uint64_t lane = 16u / rule->element;
            const uint64_t index = immediate & (count - 1);
            uint32_t r = 0;
            if (index >= lane)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, v0, 0, v1);
                row->records[r].immediate = 1;
                r += 1;
            }
            // VPEXTRW's form takes the general register in reg and the vector in rm; the others' the other way round.
            const uint8_t vector = index >= lane ? v1 : v0;
            row->records[r] = rule->form == X86_FORM_VPEXTRW_R32_X_IMM8_EVEX ? check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, vector) :
                                                                                 check_x86_lower_record(rule->form, vector, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
            row->records[r].immediate = (int32_t)(index & (lane - 1));
            row->record_count = (uint8_t)(r + 1);
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = rule->element == 1 ? BUSTER_TYPED_CLASS_U8 : rule->element == 2 ? BUSTER_TYPED_CLASS_U16 :
                                rule->element == 4 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_I64;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_INSERT)
        {
            const uint64_t count = 16u / rule->element;
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, BUSTER_TIER0_ROLE_INTEGER(1));
            row->records[0].immediate = (int64_t)(immediate & (count - 1));
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = rule->element == 8 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
        }
        else if (rule->family == CHECK_X86_LOWER_MOVEMASK)
        {
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_GENERAL)
        {
            const uint8_t integer = rule->bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, BUSTER_TIER0_ROLE_INTEGER(rule->swap ? 1 : 0),
                                                     BUSTER_TIER0_ROLE_INTEGER(rule->swap ? 0 : 1));
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = integer;
            row->operand_classes[1] = integer;
            row->result_class = integer;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_UNDEFINED)
        {
            row->records[0] = check_x86_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v0, v0, v0);
            row->record_count = 1;
        }
        else if (rule->family == CHECK_X86_LOWER_KBINARY || rule->family == CHECK_X86_LOWER_KSHIFT)
        {
            // a in k1 (and b in k2), the form into k1, and k1 back; KSHIFTL's count its immediate's low byte.
            const bool shift = rule->family == CHECK_X86_LOWER_KSHIFT;
            uint32_t r = 0;
            row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            if (shift)
            {
                row->records[r] = check_x86_lower_record(rule->form, 1, 0, 1);
                row->records[r++].immediate = (int32_t)(immediate & 0xff);
            }
            else
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 2, 0, BUSTER_TIER0_ROLE_INTEGER(1));
                row->records[r++] = check_x86_lower_record(rule->form, 1, 1, 2);
            }
            row->records[r++] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = (uint8_t)r;
            row->operand_count = shift ? 1 : 2;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_COMPRESS)
        {
            // w in VECTOR(1) takes a's elements under the mask, compressed (the form's rm is its destination) or expanded
            // (its reg is), then goes to VECTOR(0).
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = rule->swap ? check_x86_lower_record(rule->form, v1, 0, v0) : check_x86_lower_record(rule->form, v0, 0, v1);
            row->records[1].decorations = 1;
            row->records[2] = check_x86_lower_move_result(rule->bytes, v1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_COMPRESS_STORE || rule->family == CHECK_X86_LOWER_TRUNCATE_STORE)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = (X86Instruction){ .form = rule->form, .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 };
            row->record_count = 2;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = BUSTER_TYPED_CLASS_VOID;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_VECTOR_MASK)
        {
            row->records[0] = check_x86_lower_record(rule->form, 1, 0, v0);
            row->records[1] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_MASK_VECTOR)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(rule->form, v0, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_CONVERT)
        {
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = rule->other == 8 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_EXTRACT_LANE || rule->family == CHECK_X86_LOWER_TRUNCATE)
        {
            // The lane at the index (its low bits), or the truncation, into w's register under the mask (the form's rm
            // is its destination), then to VECTOR(0) at w's bytes.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = check_x86_lower_record(rule->form, v0, 0, v1);
            row->records[1].decorations = 1;
            if (rule->family == CHECK_X86_LOWER_EXTRACT_LANE)
            {
                row->records[1].immediate = (int32_t)(immediate & ((uint64_t)rule->bytes / rule->other - 1));
            }
            row->records[2] = check_x86_lower_move_result(rule->other, v1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other;
            row->operand_classes[2] = mask_class;
            row->result_bytes = rule->other;
        }
        else if (rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE)
        {
            // An insertion's lane index is its immediate's low bits (VINSERTI's), the others' the whole byte.
            const uint64_t lanes_in = rule->other != 0 ? (uint64_t)rule->bytes / rule->other : 256;
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, v1);
            row->records[0].immediate = (int32_t)(immediate & (lanes_in - 1));
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other != 0 ? rule->other : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE || rule->family == CHECK_X86_LOWER_UNARY)
        {
            row->records[0] = rule->swap ? check_x86_lower_record(rule->form, 0, v0, v0) : check_x86_lower_record(rule->form, v0, 0, v0);
            row->records[0].immediate = rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE ? (int32_t)(immediate & 0xff) : 0;
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_TERNARY)
        {
            // VPTERNLOG's (the one with an immediate) under its mask, merged into a.
            const bool ternary_logic = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE;
            uint32_t r = 0;
            if (ternary_logic)
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(3));
            }
            row->records[r] = check_x86_lower_record(rule->form, v0, v1, v2);
            row->records[r].immediate = ternary_logic ? (int32_t)(immediate & 0xff) : 0;
            row->records[r].decorations = ternary_logic ? 1 : 0;
            row->record_count = (uint8_t)(r + 1);
            row->operand_count = ternary_logic ? 4 : 3;
            for (uint32_t j = 0; j < 3; j += 1)
            {
                row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = rule->bytes;
            }
            row->operand_classes[3] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_GATHER || rule->family == CHECK_X86_LOWER_SCATTER)
        {
            // The scale is the immediate: 1, 2, 4 or 8 (clang checks it), its log2 the address's.
            const uint32_t scale = immediate == 1 ? 0 : immediate == 2 ? 1 : immediate == 4 ? 2 : immediate == 8 ? 3 : 4;
            const bool gather = rule->family == CHECK_X86_LOWER_GATHER;
            reason = scale == 4 ? "x86 builtins' gathers and scatters of a scale other than 1, 2, 4 or 8" : reason;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(gather ? 3 : 1));
            row->records[1] = (X86Instruction){ .form = rule->form, .reg = gather ? v0 : v3, .rm = BUSTER_TIER0_ROLE_INTEGER(gather ? 1 : 0), .index = v2,
                                                .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale & 3), .decorations = 1 };
            row->record_count = 2;
            row->operand_count = 4;
            if (gather)
            {
                row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[0] = rule->bytes;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->other;
                row->operand_classes[3] = mask_class;
            }
            else
            {
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = mask_class;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->bytes;
                row->operand_classes[3] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[3] = rule->other;
                row->result_class = BUSTER_TYPED_CLASS_VOID;
                row->result_bytes = 0;
            }
        }
        else if (rule->family == CHECK_X86_LOWER_BLENDV)
        {
            // The mask's vector, VECTOR(2), in the immediate's bits 7 to 4.
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, v1);
            row->records[0].immediate = 2 << 4;
            row->record_count = 1;
            row->operand_count = 3;
            for (uint32_t j = 0; j < 3; j += 1)
            {
                row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = rule->bytes;
            }
        }
        else
        {
            // MASK_BINARY: the form's mask into k2 under the mask in k1.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = check_x86_lower_record(rule->form, 2, v0, v1);
            row->records[1].decorations = 1;
            row->records[2] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 2);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
    }
    if (reason == NULL && (target & needed) != needed)
    {
        reason = "x86 builtins whose instructions the target hasn't";
    }
    return reason;
}
