// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU C's vector operators as Tier 0's machine operations (#136, #133): the row of an X86 entry (Tier0Operation) for an
// operator at a vector's element type and bytes, as clang -O0's code computes it, an instruction per element type where
// AVX-512 has one. Each row's forms are EVEX ones of AVX-512F and AVX-512VL, which the lowering requires of a function with
// vectors (check_lower_vector_target), and AVX-512BW's or AVX-512DQ's where the row names them, and AVX512-FP16's for
// half-precision elements' arithmetic and compares (its extension: X86Target.extensions); an operator AVX-512 has no
// instruction for (integer division and remainder, 8-bit multiplication and shifts) is refused by name, which clang
// computes an element at a time. Included by c_check_program_lower.c after its class helpers.

// What a row computes: one of C's operators on vectors (CHECK_OP_ADD ... CHECK_OP_NOT_EQUAL, and the unary
// CHECK_OP_NEGATIVE and CHECK_OP_BIT_NOT), or a splat of a scalar operand into every element.
#define BUSTER_CHECK_VECTOR_SPLAT CHECK_OP_COUNT

// A vector's element, as the rows read it: its bytes (1, 2, 4 or 8), and whether it is floating (_Float16, float or
// double) or a signed integer.
typedef struct CheckVectorElement CheckVectorElement;
struct CheckVectorElement
{
    uint8_t bytes;
    bool floating;
    bool is_signed;
};

// The element of a vector type the rows take, or bytes 0 for one they don't: __bf16, long double and __int128 elements,
// and bool's.
static CheckVectorElement check_vector_lower_element(const TypeTable* types, TypeId vector)
{
    const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, type_unqualified(type_strip_aligned(types, vector)))));
    CheckVectorElement result = { 0 };
    uint32_t bits = 0;
    bool sign = false;
    if (element == BUSTER_TYPE_FLOAT16 || element == BUSTER_TYPE_FLOAT || element == BUSTER_TYPE_DOUBLE)
    {
        result = (CheckVectorElement){ .bytes = element == BUSTER_TYPE_FLOAT16 ? 2 : element == BUSTER_TYPE_FLOAT ? 4 : 8, .floating = true, .is_signed = true };
    }
    else if (element != BUSTER_TYPE_BOOL && check_action_integer_type(types, element, &bits, &sign) && (bits == 8 || bits == 16 || bits == 32 || bits == 64))
    {
        result = (CheckVectorElement){ .bytes = (uint8_t)(bits / 8), .is_signed = sign };
    }
    return result;
}

// A form at each of the three widths: 16, 32 and 64 bytes.
typedef struct CheckVectorForms CheckVectorForms;
struct CheckVectorForms
{
    uint16_t forms[3];
};

#define BUSTER_CHECK_VECTOR_FORMS(x, y, z) ((CheckVectorForms){ { X86_FORM_##x, X86_FORM_##y, X86_FORM_##z } })
// A family with VEX and EVEX forms at 16 and 32 bytes: the EVEX ones, which reach the vector registers past 15.
#define BUSTER_CHECK_VECTOR_EVEX(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_X_XM128_EVEX, name##_Y_Y_YM256_EVEX, name##_Z_Z_ZM512)
// A family of EVEX forms alone.
#define BUSTER_CHECK_VECTOR_PACKED(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_X_XM128, name##_Y_Y_YM256, name##_Z_Z_ZM512)

// The forms of a two-operand operator at an element, written dest, first, second (reg, vvvv, rm): forms 0 for none.
// `features` gets what the forms need past AVX-512F and AVX-512VL.
static CheckVectorForms check_vector_lower_binary_forms(CheckOp op, CheckVectorElement element, uint64_t* features)
{
    const uint32_t b = element.bytes;
    CheckVectorForms forms = { { 0, 0, 0 } };
    uint64_t needed = 0;
    if (element.floating && b == 2)
    {
        forms = op == CHECK_OP_ADD      ? BUSTER_CHECK_VECTOR_PACKED(VADDPH) :
                op == CHECK_OP_SUBTRACT ? BUSTER_CHECK_VECTOR_PACKED(VSUBPH) :
                op == CHECK_OP_MULTIPLY ? BUSTER_CHECK_VECTOR_PACKED(VMULPH) :
                op == CHECK_OP_DIVIDE   ? BUSTER_CHECK_VECTOR_PACKED(VDIVPH) :
                                          forms;
    }
    else if (element.floating)
    {
        const bool single = b == 4;
        forms = op == CHECK_OP_ADD      ? (single ? BUSTER_CHECK_VECTOR_EVEX(VADDPS) : BUSTER_CHECK_VECTOR_EVEX(VADDPD)) :
                op == CHECK_OP_SUBTRACT ? (single ? BUSTER_CHECK_VECTOR_EVEX(VSUBPS) : BUSTER_CHECK_VECTOR_EVEX(VSUBPD)) :
                op == CHECK_OP_MULTIPLY ? (single ? BUSTER_CHECK_VECTOR_EVEX(VMULPS) : BUSTER_CHECK_VECTOR_EVEX(VMULPD)) :
                op == CHECK_OP_DIVIDE   ? (single ? BUSTER_CHECK_VECTOR_EVEX(VDIVPS) : BUSTER_CHECK_VECTOR_EVEX(VDIVPD)) :
                                          forms;
    }
    else if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        const bool add = op == CHECK_OP_ADD;
        forms = b == 1 ? (add ? BUSTER_CHECK_VECTOR_EVEX(VPADDB) : BUSTER_CHECK_VECTOR_EVEX(VPSUBB)) :
                b == 2 ? (add ? BUSTER_CHECK_VECTOR_EVEX(VPADDW) : BUSTER_CHECK_VECTOR_EVEX(VPSUBW)) :
                b == 4 ? (add ? BUSTER_CHECK_VECTOR_EVEX(VPADDD) : BUSTER_CHECK_VECTOR_EVEX(VPSUBD)) :
                         (add ? BUSTER_CHECK_VECTOR_EVEX(VPADDQ) : BUSTER_CHECK_VECTOR_EVEX(VPSUBQ));
        needed = b <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    else if (op == CHECK_OP_MULTIPLY && b != 1)
    {
        forms = b == 2 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLW) : b == 4 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLD) : BUSTER_CHECK_VECTOR_PACKED(VPMULLQ);
        needed = b == 2 ? BUSTER_X86_FEATURE(AVX512BW) : b == 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
    }
    else if (op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR)
    {
        forms = op == CHECK_OP_BIT_AND ? BUSTER_CHECK_VECTOR_PACKED(VPANDQ) : op == CHECK_OP_BIT_OR ? BUSTER_CHECK_VECTOR_PACKED(VPORQ) :
                                                                                                   BUSTER_CHECK_VECTOR_PACKED(VPXORQ);
    }
    else if ((op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT) && b != 1)
    {
        const bool left = op == CHECK_OP_SHIFT_LEFT;
        const bool arithmetic = !left && element.is_signed;
        forms = b == 2 ? (left ? BUSTER_CHECK_VECTOR_PACKED(VPSLLVW) : arithmetic ? BUSTER_CHECK_VECTOR_PACKED(VPSRAVW) : BUSTER_CHECK_VECTOR_PACKED(VPSRLVW)) :
                b == 4 ? (left ? BUSTER_CHECK_VECTOR_EVEX(VPSLLVD) : arithmetic ? BUSTER_CHECK_VECTOR_EVEX(VPSRAVD) : BUSTER_CHECK_VECTOR_EVEX(VPSRLVD)) :
                         (left ? BUSTER_CHECK_VECTOR_EVEX(VPSLLVQ) : arithmetic ? BUSTER_CHECK_VECTOR_PACKED(VPSRAVQ) : BUSTER_CHECK_VECTOR_EVEX(VPSRLVQ));
        needed = b == 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    *features = needed;
    return forms;
}

// A compare's predicate as VPCMP's and VPCMPU's immediates take it (EQ 0, LT 1, LE 2, NE 4, NLT 5, NLE 6), or VCMPPH's,
// VCMPPS's and VCMPPD's for floating elements, as clang -O0 compares them (fcmp oeq, une, olt, ole, ogt, oge): EQ_OQ,
// NEQ_UQ, LT_OS, LE_OS, GT_OS and GE_OS.
static uint8_t check_vector_lower_predicate(CheckOp op, bool floating)
{
    static const uint8_t integer[] = { 1, 2, 6, 5, 0, 4 };
    static const uint8_t real[] = { 0x01, 0x02, 0x0e, 0x0d, 0x00, 0x04 };
    const uint32_t index = (uint32_t)(op - CHECK_OP_LESS);
    return floating ? real[index] : integer[index];
}

static X86Instruction check_vector_lower_record(uint32_t form, uint8_t reg, uint8_t vvvv, uint8_t rm)
{
    return (X86Instruction){ .form = (uint16_t)form, .reg = reg, .vvvv = vvvv, .rm = rm, .mode = BUSTER_X86_REGISTER_DIRECT };
}

// The row of an operator (check_vector_lower_*'s ops, or BUSTER_CHECK_VECTOR_SPLAT) at a vector type, on a target: its
// records, operands and result, or the reason it has none. A binary operator takes its operands as VECTOR(0) and
// VECTOR(1) and leaves its result in VECTOR(0); a compare, its -1 or 0 elements of the operands' sizes; a unary one, its
// operand as VECTOR(0); a floating negation, also the sign bit as an integer operand (pushed after the vector: operand 1,
// as a role's j is its operand's place among them all); a splat, the scalar as its element's class (`scalar_class`). The
// rows don't depend on the operands' depths: Tier 0 places them. A floating division of a vector of 4 or 8 bytes
// divides its own elements alone, under a mask of them. Half-precision elements' rows but a negation's, which
// flips their bits alone, take AVX512-FP16; a splat into them has none, as a scalar of their type has no class yet (the
// checker and the scalar lowering refuse one first). With `excess` (clang's _Float16 excess precision, where the unit's
// target hasn't AVX512-FP16: check_lower_vector_half_chain), a negation of them is clang's in float, which quiets their
// signaling NaNs: the sign bits flipped, then each half converted to a float and back (VCVTPH2PS and VCVTPS2PH, exact
// but for those NaNs), the 64-byte vector's in two halves of 32 bytes.
static const char* check_vector_lower_row(const TypeTable* types, const X86Target* target, bool excess, uint32_t op, TypeId vector, uint8_t scalar_class,
                                          Tier0Operation* row)
{
    const CheckVectorElement element = check_vector_lower_element(types, vector);
    const bool half = element.floating && element.bytes == 2;
    const uint64_t bytes = type_size(types, type_unqualified(type_strip_aligned(types, vector)));
    // A vector of 4 or 8 bytes takes the 16-byte forms, in an XMM register whose bytes past it Tier 0 zeroes.
    const uint32_t width = bytes <= 16 ? 0 : bytes == 32 ? 1 : 2;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const bool compare = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)bytes;
    if (element.bytes == 0)
    {
        reason = "vector operations on elements of this type";
    }
    else if (op == BUSTER_CHECK_VECTOR_SPLAT && half)
    {
        reason = "vector splats of half-precision scalars";
    }
    else if (op == BUSTER_CHECK_VECTOR_SPLAT)
    {
        // The scalar, converted to the element's type already, in every element.
        const uint32_t b = element.bytes;
        const CheckVectorForms forms = element.floating ? (b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VBROADCASTSS_X_XM32_EVEX, VBROADCASTSS_Y_XM32_EVEX, VBROADCASTSS_Z_XM32) :
                                                                    BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTQ_X_XM64_EVEX, VBROADCASTSD_Y_XM64_EVEX, VBROADCASTSD_Z_XM64)) :
                                       b == 1 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTB_X_R32, VPBROADCASTB_Y_R32, VPBROADCASTB_Z_R32) :
                                       b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTW_X_R32, VPBROADCASTW_Y_R32, VPBROADCASTW_Z_R32) :
                                       b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTD_X_R32, VPBROADCASTD_Y_R32, VPBROADCASTD_Z_R32) :
                                                BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTQ_X_R64, VPBROADCASTQ_Y_R64, VPBROADCASTQ_Z_R64);
        needed = !element.floating && b <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        row->records[0] = check_vector_lower_record(forms.forms[width], v0, 0, element.floating ? BUSTER_TIER0_ROLE_FLOATING(0) : BUSTER_TIER0_ROLE_INTEGER(0));
        row->record_count = 1;
        row->operand_count = 1;
        row->operand_classes[0] = scalar_class;
    }
    else if (op == CHECK_OP_NEGATIVE && element.floating)
    {
        // The sign bit flipped (clang's fneg): the sign bit, an integer operand, in every element, XORed in.
        const bool wide = element.bytes == 8;
        const CheckVectorForms splat = half ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTW_X_R32, VPBROADCASTW_Y_R32, VPBROADCASTW_Z_R32) :
                                       wide ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTQ_X_R64, VPBROADCASTQ_Y_R64, VPBROADCASTQ_Z_R64) :
                                              BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTD_X_R32, VPBROADCASTD_Y_R32, VPBROADCASTD_Z_R32);
        needed = half ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        row->records[0] = check_vector_lower_record(splat.forms[width], v1, 0, BUSTER_TIER0_ROLE_INTEGER(1));
        row->records[1] = check_vector_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v0, v0, v1);
        row->record_count = 2;
        if (half && excess && width < 2)
        {
            // The halves to floats in v1, and back into v0 (VCVTPS2PH's rm is its destination) at the current rounding.
            row->records[2] = check_vector_lower_record(width == 0 ? X86_FORM_VCVTPH2PS_Y_XM128_EVEX : X86_FORM_VCVTPH2PS_Z_YM256, v1, 0, v0);
            row->records[3] = check_vector_lower_record(width == 0 ? X86_FORM_VCVTPS2PH_XM128_Y_IMM8_EVEX : X86_FORM_VCVTPS2PH_YM256_Z_IMM8, v1, 0, v0);
            row->records[3].immediate = 4;
            row->record_count = 4;
        }
        else if (half && excess)
        {
            // The low 32 bytes' through v1 and the high ones' through v2, then both into v0.
            const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
            row->records[2] = check_vector_lower_record(X86_FORM_VCVTPH2PS_Z_YM256, v1, 0, v0);
            row->records[3] = check_vector_lower_record(X86_FORM_VCVTPS2PH_YM256_Z_IMM8, v1, 0, v1);
            row->records[3].immediate = 4;
            row->records[4] = check_vector_lower_record(X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, v0, 0, v2);
            row->records[4].immediate = 1;
            row->records[5] = check_vector_lower_record(X86_FORM_VCVTPH2PS_Z_YM256, v2, 0, v2);
            row->records[6] = check_vector_lower_record(X86_FORM_VCVTPS2PH_YM256_Z_IMM8, v2, 0, v2);
            row->records[6].immediate = 4;
            row->records[7] = check_vector_lower_record(X86_FORM_VINSERTI64X4_Z_Z_YM256_IMM8, v0, v1, v2);
            row->records[7].immediate = 1;
            row->record_count = 8;
        }
        row->operand_count = 2;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)bytes;
        row->operand_classes[1] = wide ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        if (op == CHECK_OP_NEGATIVE)
        {
            // 0 - the operand.
            uint64_t features = 0;
            const CheckVectorForms subtract = check_vector_lower_binary_forms(CHECK_OP_SUBTRACT, element, &features);
            needed = features;
            row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v1, v1, v1);
            row->records[1] = check_vector_lower_record(subtract.forms[width], v0, v1, v0);
            row->record_count = 2;
        }
        else
        {
            // Every bit's complement: VPTERNLOGQ's NOT C.
            row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGQ_X_X_XM128_IMM8, VPTERNLOGQ_Y_Y_YM256_IMM8, VPTERNLOGQ_Z_Z_ZM512_IMM8).forms[width],
                                                        v0, v0, v0);
            row->records[0].immediate = 0x55;
            row->record_count = 1;
        }
        row->operand_count = 1;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)bytes;
    }
    else if (compare)
    {
        // The compare's opmask in k1, then -1 in the elements whose bits are set and 0 in the others.
        const uint32_t b = element.bytes;
        const CheckVectorForms forms = element.floating ? (b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VCMPPH_K_X_XM128_IMM8, VCMPPH_K_Y_YM256_IMM8, VCMPPH_K_Z_ZM512_IMM8) :
                                                           b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VCMPPS_K_X_XM128_IMM8, VCMPPS_K_Y_YM256_IMM8, VCMPPS_K_Z_ZM512_IMM8) :
                                                                    BUSTER_CHECK_VECTOR_FORMS(VCMPPD_K_X_XM128_IMM8, VCMPPD_K_Y_YM256_IMM8, VCMPPD_K_Z_ZM512_IMM8)) :
                                       element.is_signed ?
                                           (b == 1 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPB_K_X_XM128_IMM8, VPCMPB_K_Y_YM256_IMM8, VPCMPB_K_Z_ZM512_IMM8) :
                                            b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPW_K_X_XM128_IMM8, VPCMPW_K_Y_YM256_IMM8, VPCMPW_K_Z_ZM512_IMM8) :
                                            b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPD_K_X_XM128_IMM8, VPCMPD_K_Y_YM256_IMM8, VPCMPD_K_Z_ZM512_IMM8) :
                                                     BUSTER_CHECK_VECTOR_FORMS(VPCMPQ_K_X_XM128_IMM8, VPCMPQ_K_Y_YM256_IMM8, VPCMPQ_K_Z_ZM512_IMM8)) :
                                           (b == 1 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPUB_K_X_XM128_IMM8, VPCMPUB_K_Y_YM256_IMM8, VPCMPUB_K_Z_ZM512_IMM8) :
                                            b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPUW_K_X_XM128_IMM8, VPCMPUW_K_Y_YM256_IMM8, VPCMPUW_K_Z_ZM512_IMM8) :
                                            b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VPCMPUD_K_X_XM128_IMM8, VPCMPUD_K_Y_YM256_IMM8, VPCMPUD_K_Z_ZM512_IMM8) :
                                                     BUSTER_CHECK_VECTOR_FORMS(VPCMPUQ_K_X_XM128_IMM8, VPCMPUQ_K_Y_YM256_IMM8, VPCMPUQ_K_Z_ZM512_IMM8));
        const CheckVectorForms masks = b == 1 ? BUSTER_CHECK_VECTOR_FORMS(VPMOVM2B_X_K, VPMOVM2B_Y_K, VPMOVM2B_Z_K) :
                                       b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VPMOVM2W_X_K, VPMOVM2W_Y_K, VPMOVM2W_Z_K) :
                                       b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VPMOVM2D_X_K, VPMOVM2D_Y_K, VPMOVM2D_Z_K) :
                                                BUSTER_CHECK_VECTOR_FORMS(VPMOVM2Q_X_K, VPMOVM2Q_Y_K, VPMOVM2Q_Z_K);
        needed = (b <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0) | (b >= 4 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
        row->records[0] = check_vector_lower_record(forms.forms[width], 1, v0, v1);
        row->records[0].immediate = check_vector_lower_predicate((CheckOp)op, element.floating);
        row->records[1] = check_vector_lower_record(masks.forms[width], v0, 0, 1);
        row->record_count = 2;
        row->operand_count = 2;
    }
    else
    {
        uint64_t features = 0;
        const CheckVectorForms forms = check_vector_lower_binary_forms((CheckOp)op, element, &features);
        needed = features;
        if (forms.forms[width] == 0)
        {
            reason = element.floating ? "this operator on floating vectors" :
                     op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER ? "vector division of integers" :
                     op == CHECK_OP_MULTIPLY ? "vector multiplication of 8-bit elements" :
                     op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT ? "vector shifts of 8-bit elements" : "this vector operator";
        }
        if (element.floating && op == CHECK_OP_DIVIDE && bytes < 16)
        {
            // A vector of 4 or 8 bytes divides its own elements alone, under k1's mask of them with zeroing: its
            // register's bytes past it are zeros, whose 0/0 would raise FE_INVALID, and masked-out elements raise no
            // exception (clang's code divides them one at a time under FENV_ACCESS).
            row->records[0] = check_vector_lower_record(X86_FORM_MOV_R32_IMM32, 0, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
            row->records[0].immediate = (int64_t)((UINT64_C(1) << (bytes / element.bytes)) - 1);
            row->records[1] = check_vector_lower_record(X86_FORM_KMOVW_K_R32, 1, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
            row->records[2] = check_vector_lower_record(forms.forms[width], v0, v0, v1);
            row->records[2].decorations = 1 | BUSTER_X86_DECORATION_ZEROING;
            row->record_count = 3;
        }
        else
        {
            row->records[0] = check_vector_lower_record(forms.forms[width], v0, v0, v1);
            row->record_count = 1;
        }
        row->operand_count = 2;
    }
    if (compare || (row->operand_count == 2 && row->operand_classes[0] == 0))
    {
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)bytes;
        row->operand_bytes[1] = (uint8_t)bytes;
    }
    if (reason == NULL && (target->features & needed) != needed)
    {
        reason = (needed & BUSTER_X86_FEATURE(AVX512BW)) != 0 && (target->features & BUSTER_X86_FEATURE(AVX512BW)) == 0 ?
                     "vector operations on 8- and 16-bit elements without AVX-512BW" :
                     "vector operations without AVX-512DQ";
    }
    if (reason == NULL && half && op != CHECK_OP_NEGATIVE && (target->extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0)
    {
        reason = "vector operations on half-precision elements without AVX512-FP16";
    }
    return reason;
}
