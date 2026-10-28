// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The intrinsics' x86 builtins as Tier 0's machine operations (#136, #148): the row of an X86 entry (Tier0Operation) for a
// builtin's call, its arguments converted to its prototype's types already (c_check_builtin_x86.c) and its immediates
// constants, as clang -O0's code computes it. A builtin's row is its family's with the rule's form, from a table by
// name; the families are those this compiler's own main.c reaches first. Each form is a VEX or EVEX one that names its
// operands in its fields alone, of AVX-512F and AVX-512VL, which the lowering requires of a function with vectors, and
// of the features the rule names past them. A builtin the table doesn't name is refused by name, and so is one whose
// instruction names fixed registers (rdtsc, xgetbv, monitorx and mwaitx) or has a legacy encoding (pause, tzcnt), which
// no row takes. Included by c_check_program_lower.c after c_check_program_vectors.c.

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
    // The family's form: a BINARY's, SHIFT's, EXTRACT's, INSERT's, MOVEMASK's or GENERAL's.
    uint16_t form;
    // Its features past AVX-512F and AVX-512VL.
    uint64_t features;
};

#define BUSTER_CHECK_X86_BW BUSTER_X86_FEATURE(AVX512BW)
#define BUSTER_CHECK_X86_RULE(name, family, element, bytes, is_signed, swap, immediate, form, features) \
    { name, sizeof(name) - 1, family, element, bytes, is_signed, swap, immediate, form, features },
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
};

#undef BUSTER_CHECK_X86_SHIFTS
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
        const uint32_t lanes = rule->family == CHECK_X86_LOWER_KMOV ? 8u * rule->bytes : (uint32_t)rule->bytes / rule->element;
        const uint8_t mask_class = check_x86_lower_mask_class(lanes);
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
        const bool masked = rule->family <= CHECK_X86_LOWER_STORE;
        needed = rule->features | (masked && rule->element <= 2 && rule->family != CHECK_X86_LOWER_KMOV ? BUSTER_CHECK_X86_BW : 0) |
                 (masked && lanes >= 32 ? BUSTER_CHECK_X86_BW : 0);
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
            // The element at the index's low bits, zero-extended into the general register (but VPEXTRQ's, whole).
            const uint64_t count = 16u / rule->element;
            row->records[0] = check_x86_lower_record(rule->form, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
            row->records[0].immediate = (int64_t)(immediate & (count - 1));
            row->record_count = 1;
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
        else
        {
            row->records[0] = check_x86_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v0, v0, v0);
            row->record_count = 1;
        }
    }
    if (reason == NULL && (target & needed) != needed)
    {
        reason = "x86 builtins whose instructions the target hasn't";
    }
    return reason;
}
