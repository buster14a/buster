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
    // (mask m, vector a, vector w): w with its first element a's where m's first bit is set, and a's others (clang's
    // scalar select: the form, VMOVSS's or VMOVSD's of three registers, merges a's first element into w's under m).
    CHECK_X86_LOWER_SELECT_SCALAR,
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
    // (vector a[, int immediate], vector w, mask m): the form's operation of a (rm) into w (reg) under m, of `other` bytes
    // where they aren't a's (a conversion's); `element` is a's bytes per lane of the mask, which are a's bytes over it
    // (VRCP14PS, VGETEXPPS, VCVTPD2DQ, VGETMANTPS).
    CHECK_X86_LOWER_MASK_UNARY,
    // (vector a, vector b[, int immediate], vector w, mask m): the form's operation of a (vvvv) and b (rm) into w (reg)
    // under m, b of `other` bytes where they aren't a's (VSCALEFPS, VRCP14SS, VMAXSD, VCVTSD2SS).
    CHECK_X86_LOWER_MASK_MERGE,
    // (vector a): the form's dest, a, a: a scalar operation of a's first element, the others a's (VRCPSS, VRSQRTSS).
    CHECK_X86_LOWER_UNARY_SCALAR,
} CheckX86LowerFamily;

// A rule's rounding control, the builtin's last argument (an immediate): none, the exceptions' suppression (SAE: 8 or 12,
// _MM_FROUND_NO_EXC, take the rule's round_form) or embedded rounding (ER: 8 to 11, _MM_FROUND_NO_EXC with a direction
// in the low bits, take round_form with that rounding); 4, _MM_FROUND_CUR_DIRECTION, takes the rule's form either way.
// clang refuses other values.
typedef enum CheckX86LowerRounding
{
    CHECK_X86_ROUND_NONE,
    CHECK_X86_ROUND_SAE,
    CHECK_X86_ROUND_ER,
} CheckX86LowerRounding;

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
    // The form its rounding control picks (CheckX86LowerRounding), and that control's kind.
    uint16_t round_form;
    uint8_t rounding;
    // BINARY's immediate where its form has one and the builtin none: the SSE compares' predicates.
    uint8_t fixed;
};

#define BUSTER_CHECK_X86_BW BUSTER_X86_FEATURE(AVX512BW)
#define BUSTER_CHECK_X86_RULE(name, family, element, bytes, is_signed, swap, immediate, form, features) \
    { name, sizeof(name) - 1, family, element, bytes, is_signed, swap, immediate, form, 0, false, features },
// A rule with another vector's bytes, or KBINARY's zero test.
#define BUSTER_CHECK_X86_RULE_OTHER(name, family, element, bytes, swap, immediate, form, other, features) \
    { name, sizeof(name) - 1, family, element, bytes, false, swap, immediate, form, other, false, features },
#define BUSTER_CHECK_X86_RULE_MASK(name, family, bytes, form, zero_test, features) \
    { name, sizeof(name) - 1, family, 1, bytes, false, false, family == CHECK_X86_LOWER_KSHIFT ? 1 : BUSTER_CHECK_X86_NO_IMMEDIATE, form, 0, zero_test, features },
// A rule of one width in short: its name past "__builtin_ia32_", its family's and its forms' names past their prefixes,
// with another vector's bytes (and `swap`), and with a rounding control (its last argument) and the form it picks.
#define BUSTER_CHECK_X86_ONE(name, family, element, bytes, form, features) \
    BUSTER_CHECK_X86_RULE("__builtin_ia32_" name, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, features)
#define BUSTER_CHECK_X86_ONE_OTHER(name, family, element, bytes, swap, form, other, features) \
    BUSTER_CHECK_X86_RULE_OTHER("__builtin_ia32_" name, CHECK_X86_LOWER_##family, element, bytes, swap, BUSTER_CHECK_X86_NO_IMMEDIATE, X86_FORM_##form, other, \
                                features)
#define BUSTER_CHECK_X86_ROUNDED(name, family, element, bytes, form, other, features, round_form, rounding) \
    { "__builtin_ia32_" name, sizeof("__builtin_ia32_" name) - 1, CHECK_X86_LOWER_##family, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, \
      X86_FORM_##form, other, false, features, X86_FORM_##round_form, CHECK_X86_ROUND_##rounding, 0 },
// A BINARY rule whose form's immediate is `fixed`.
#define BUSTER_CHECK_X86_RULE_FIXED(name, element, bytes, form, fixed, features) \
    { name, sizeof(name) - 1, CHECK_X86_LOWER_BINARY, element, bytes, false, false, BUSTER_CHECK_X86_NO_IMMEDIATE, form, 0, false, features, 0, 0, fixed },
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

static const CheckX86LowerRule check_x86_lower_rules[] = {
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
};

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
    case CHECK_X86_LOWER_SELECT_SCALAR:
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
    case CHECK_X86_LOWER_TRUNCATE_STORE:
    case CHECK_X86_LOWER_MASK_UNARY:
    case CHECK_X86_LOWER_MASK_MERGE: result = true; break;
    default: break;
    }
    return result;
}

// A record of a rule with a rounding control at that control's value, `round` (CheckX86LowerRounding): its form, or the
// rule's round_form with the direction in its rounding bits (BUSTER_X86_DECORATION_ROUNDING's, the low bits' order:
// nearest, down, up, zero).
static void check_x86_lower_round(const CheckX86LowerRule* rule, uint64_t round, X86Instruction* record)
{
    if (rule->rounding != CHECK_X86_ROUND_NONE && (round & 8) != 0)
    {
        record->form = rule->round_form;
        if (rule->rounding == CHECK_X86_ROUND_ER)
        {
            record->decorations = (uint8_t)(record->decorations | (round & 3) << 5);
        }
    }
}

// The row of an x86 builtin's call on a target: its records, operands and result, or the reason it has none.
// `immediate` is its immediate argument's constant value (the rule's `immediate`'s), which a record takes and no operand
// is, and `round` its rounding control's (its last argument's, where the rule has one). Operands are the other arguments
// in order, and a role's j is its operand's place among them all, so that a compare's mask is INTEGER(2) and a select's
// vectors VECTOR(1) and VECTOR(2). An integer operand's class here is its width's; the caller gives each its argument's
// own (check_lower_x86_builtin). A result is in the class the instruction extends it to, which the caller converts to
// the builtin's type.
static const char* check_x86_lower_row_of(uint64_t target, const CheckX86LowerRule* rule, uint64_t immediate, uint64_t round, Tier0Operation* row)
{
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
        else if (rule->family == CHECK_X86_LOWER_SELECT_SCALAR)
        {
            // W's elements, then A's first merged in where the mask's first bit is set and A's others over W's.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v2);
            row->records[2] = check_x86_lower_record(rule->form, v0, v1, v1);
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
            // The load merges into W's register and leaves it in VECTOR(0); the store writes A's elements alone. The move
            // is the rule's form where it names one (the aligned and floating moves, and the expanding loads), else the
            // element's VMOVDQU.
            const bool store = rule->family == CHECK_X86_LOWER_STORE;
            const uint16_t move = rule->form != X86_FORM_NONE ? rule->form : check_x86_lower_move(rule->element, width, store);
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = (X86Instruction){ .form = move, .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 };
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
            // b is of `other` bytes where they aren't a's (a shift's count, an XMM register at every width).
            row->records[0] = check_x86_lower_record(rule->form, v0, rule->swap ? v1 : v0, rule->swap ? v0 : v1);
            row->records[0].immediate = rule->fixed;
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other != 0 ? rule->other : rule->bytes;
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
            // The result is of `other` bytes where they aren't a's (a narrowing conversion's, VCVTPD2DQ's of a YMM).
            row->records[0] = rule->swap ? check_x86_lower_record(rule->form, 0, v0, v0) : check_x86_lower_record(rule->form, v0, 0, v0);
            row->records[0].immediate = rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE ? (int32_t)(immediate & 0xff) : 0;
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_bytes = rule->other != 0 ? rule->other : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_UNARY_SCALAR)
        {
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, v0);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_MASK_UNARY || rule->family == CHECK_X86_LOWER_MASK_MERGE)
        {
            // The operation into w's register under the mask, then w to VECTOR(0) at its bytes. A MASK_UNARY's w is
            // VECTOR(1) and its mask INTEGER(2); a MASK_MERGE's b VECTOR(1), w VECTOR(2) and its mask INTEGER(3).
            const bool merge = rule->family == CHECK_X86_LOWER_MASK_MERGE;
            const uint8_t w = merge ? v2 : v1;
            const uint32_t w_bytes = !merge && rule->other != 0 ? rule->other : rule->bytes;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(merge ? 3 : 2));
            row->records[1] = check_x86_lower_record(rule->form, w, merge ? v0 : 0, merge ? v1 : v0);
            row->records[1].decorations = 1;
            row->records[1].immediate = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE ? (int32_t)(immediate & 0xff) : 0;
            check_x86_lower_round(rule, round, &row->records[1]);
            row->records[2] = check_x86_lower_move_result(w_bytes, w);
            row->record_count = 3;
            row->operand_count = merge ? 4 : 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = merge && rule->other != 0 ? rule->other : w_bytes;
            row->operand_classes[merge ? 3 : 2] = mask_class;
            if (merge)
            {
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->bytes;
            }
            row->result_bytes = w_bytes;
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
