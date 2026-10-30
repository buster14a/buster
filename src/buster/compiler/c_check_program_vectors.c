// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU C's vector operators as Tier 0's machine operations (#136, #133): the row of an X86 entry (Tier0Operation) for an
// operator at a vector's element type and bytes, as clang -O0's code computes it, an instruction per element type where
// AVX-512 has one. Each row's forms are EVEX ones of AVX-512F and AVX-512VL, which the lowering requires of a function with
// vectors (check_lower_vector_target), and AVX-512BW's or AVX-512DQ's where the row names them, and AVX512-FP16's for
// half-precision elements' arithmetic and compares (its extension: X86Target.extensions); bf16 elements' arithmetic is
// AVX10.2's, or float's around AVX512-BF16's rounding where clang computes it so (check_vector_lower_brain_rows); an
// operator AVX-512 has no instruction for (integer division and remainder, 8-bit multiplication and shifts) is refused
// by name, which clang computes an element at a time. Included by c_check_program_lower.c after its class helpers.

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
// flips their bits alone, and a splat's, of a scalar's bits, take AVX512-FP16. With `excess` (clang's _Float16 excess precision, where the unit's
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
    else if (op == BUSTER_CHECK_VECTOR_SPLAT)
    {
        // The scalar, converted to the element's type already, in every element: a half-precision one's bits, an integer
        // operand zero-extended to 32 bits (c_check_program_half.c), as a 16-bit integer's.
        const uint32_t b = element.bytes;
        const bool bits = !element.floating || half;
        const CheckVectorForms forms = !bits ? (b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VBROADCASTSS_X_XM32_EVEX, VBROADCASTSS_Y_XM32_EVEX, VBROADCASTSS_Z_XM32) :
                                                        BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTQ_X_XM64_EVEX, VBROADCASTSD_Y_XM64_EVEX, VBROADCASTSD_Z_XM64)) :
                                       b == 1 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTB_X_R32, VPBROADCASTB_Y_R32, VPBROADCASTB_Z_R32) :
                                       b == 2 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTW_X_R32, VPBROADCASTW_Y_R32, VPBROADCASTW_Z_R32) :
                                       b == 4 ? BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTD_X_R32, VPBROADCASTD_Y_R32, VPBROADCASTD_Z_R32) :
                                                BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTQ_X_R64, VPBROADCASTQ_Y_R64, VPBROADCASTQ_Z_R64);
        needed = bits && b <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        row->records[0] = check_vector_lower_record(forms.forms[width], v0, 0, bits ? BUSTER_TIER0_ROLE_INTEGER(0) : BUSTER_TIER0_ROLE_FLOATING(0));
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
    if (reason == NULL && half && op != CHECK_OP_NEGATIVE && op != BUSTER_CHECK_VECTOR_SPLAT && (target->extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0)
    {
        reason = "vector operations on half-precision elements without AVX512-FP16";
    }
    return reason;
}

// Whether a vector type's elements are __bf16's (bfloat16), whose operators are check_vector_lower_brain_rows'.
static bool check_vector_lower_brain(const TypeTable* types, TypeId vector)
{
    const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, type_unqualified(type_strip_aligned(types, vector)))));
    return element == BUSTER_TYPE_BF16;
}

// The rows of an operator on vectors of `bytes` bytes of __bf16 elements: +, -, * and /, negation, and a scalar's splat
// (its bits in every element, as a half-precision one's, from an integer operand of `scalar_class`). Where the unit's
// target has AVX10.2 (`excess` false), an operator is AVX10.2's instruction (VADDBF16 to VDIVBF16, which round to
// nearest even, flush subnormals and report nothing), which the function's target must have, and a negation flips the
// sign bits. Where it hasn't, clang's bfloat16 excess precision computes them in float, as its -O0 code does: each
// operand widened to floats (VPMOVZXWD, then VPSLLD by 16: a bf16 is a float's high half), the floats' operation
// (VADDPS to VDIVPS at the current rounding; a negation XORs their sign bits), and the floats rounded back by
// AVX512-BF16's VCVTNEPS2BF16 (to nearest even, subnormals flushed, nothing reported), which the function's target must
// have; a vector of 4 bytes divides its two elements alone, under a mask of them, as a float vector of 4 or 8 bytes
// does. A row takes its operands as VECTOR(0) and VECTOR(1) and leaves its result in VECTOR(0) (`*count` 1), but a
// vector of 64 bytes under excess precision, whose operands' floats take 128 bytes each, past a row's 8 records: its
// three rows are its halves' (rows 0 and 1, which read the operands' bytes 0 to 31 and 32 to 63 through their
// addresses, integer operands of U64, the frame temporaries' that check_lower_vector_brain stores them into, and leave
// their halves' 32 bytes) and row 2, which joins the halves (VINSERTF64X4), as clang's code reads each operand's halves
// from its frame slot. Compares and the other operators are refused by name.
static const char* check_vector_lower_brain_rows(const X86Target* target, bool excess, uint32_t op, uint32_t bytes, uint8_t scalar_class, Tier0Operation* rows,
                                                 uint32_t* count)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t s0 = BUSTER_TIER0_ROLE_SCRATCH(0);
    const uint32_t width = bytes <= 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool binary = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_MULTIPLY || op == CHECK_OP_DIVIDE;
    const bool halves = excess && bytes == 64;
    const uint32_t operands = binary ? 2 : 1;
    const char* reason = NULL;
    uint64_t needed = 0;
    uint64_t extensions = 0;
    memset(rows, 0, 3 * sizeof(*rows));
    *count = 1;
    rows[0].result_class = BUSTER_TYPED_CLASS_VECTOR;
    rows[0].result_bytes = (uint8_t)bytes;
    if (op == BUSTER_CHECK_VECTOR_SPLAT)
    {
        const CheckVectorForms forms = BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTW_X_R32, VPBROADCASTW_Y_R32, VPBROADCASTW_Z_R32);
        rows[0].records[0] = check_vector_lower_record(forms.forms[width], v0, 0, BUSTER_TIER0_ROLE_INTEGER(0));
        rows[0].record_count = 1;
        rows[0].operand_count = 1;
        rows[0].operand_classes[0] = scalar_class;
        needed = BUSTER_X86_FEATURE(AVX512BW);
    }
    else if (!binary && op != CHECK_OP_NEGATIVE)
    {
        reason = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL ? "vector compares of bf16 elements" : "this operator on bf16 vectors";
    }
    else if (!excess && binary)
    {
        const CheckVectorForms forms = op == CHECK_OP_ADD      ? BUSTER_CHECK_VECTOR_PACKED(VADDBF16) :
                                       op == CHECK_OP_SUBTRACT ? BUSTER_CHECK_VECTOR_PACKED(VSUBBF16) :
                                       op == CHECK_OP_MULTIPLY ? BUSTER_CHECK_VECTOR_PACKED(VMULBF16) :
                                                                 BUSTER_CHECK_VECTOR_PACKED(VDIVBF16);
        rows[0].records[0] = check_vector_lower_record(forms.forms[width], v0, v0, v1);
        rows[0].record_count = 1;
        extensions = BUSTER_X86_EXTENSION(AVX10_2);
    }
    else if (!excess)
    {
        // The sign bit, 0x8000, in every element, XORed in.
        rows[0].records[0] = check_vector_lower_record(X86_FORM_MOV_R32_IMM32, 0, 0, s0);
        rows[0].records[0].immediate = 0x8000;
        rows[0].records[1] = check_vector_lower_record(BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTW_X_R32, VPBROADCASTW_Y_R32, VPBROADCASTW_Z_R32).forms[width], v1, 0, s0);
        rows[0].records[2] = check_vector_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v0, v0, v1);
        rows[0].record_count = 3;
        needed = BUSTER_X86_FEATURE(AVX512BW);
        extensions = BUSTER_X86_EXTENSION(AVX10_2);
    }
    else
    {
        // The floats' width: an XMM register's for 4 and 8 bytes, a YMM one's for 16 and a ZMM one's for 32 and for each
        // half of 64.
        const uint32_t f = bytes <= 8 ? 0 : bytes == 16 ? 1 : 2;
        static const uint16_t widen[3] = { X86_FORM_VPMOVZXWD_X_XM64_EVEX, X86_FORM_VPMOVZXWD_Y_XM128_EVEX, X86_FORM_VPMOVZXWD_Z_YM256 };
        static const uint16_t shift[3] = { X86_FORM_VPSLLD_X_XM128_IMM8, X86_FORM_VPSLLD_Y_YM256_IMM8, X86_FORM_VPSLLD_Z_ZM512_IMM8 };
        static const uint16_t narrow[3] = { X86_FORM_VCVTNEPS2BF16_X_XM128_EVEX, X86_FORM_VCVTNEPS2BF16_X_YM256_EVEX, X86_FORM_VCVTNEPS2BF16_Y_ZM512 };
        const CheckVectorForms floats = op == CHECK_OP_ADD      ? BUSTER_CHECK_VECTOR_EVEX(VADDPS) :
                                        op == CHECK_OP_SUBTRACT ? BUSTER_CHECK_VECTOR_EVEX(VSUBPS) :
                                        op == CHECK_OP_MULTIPLY ? BUSTER_CHECK_VECTOR_EVEX(VMULPS) :
                                                                  BUSTER_CHECK_VECTOR_EVEX(VDIVPS);
        for (uint32_t h = 0; h < (halves ? 2u : 1u); h += 1)
        {
            Tier0Operation* row = rows + h;
            X86Instruction* records = row->records;
            uint32_t r = 0;
            // Each operand's floats, from its register into VECTOR(2 + j), or from its half at its address into VECTOR(j).
            for (uint32_t j = 0; j < operands; j += 1)
            {
                const uint8_t into = (uint8_t)BUSTER_TIER0_ROLE_VECTOR(halves ? j : 2 + j);
                records[r] = halves ? (X86Instruction){ .form = widen[f], .reg = into, .rm = (uint8_t)BUSTER_TIER0_ROLE_INTEGER(j), .mode = BUSTER_X86_MEMORY_BASE,
                                                        .displacement = (int32_t)(32 * h) } :
                                      check_vector_lower_record(widen[f], into, 0, (uint8_t)BUSTER_TIER0_ROLE_VECTOR(j));
                records[r + 1] = check_vector_lower_record(shift[f], 0, into, into);
                records[r + 1].immediate = 16;
                r += 2;
            }
            const uint8_t a = halves ? v0 : BUSTER_TIER0_ROLE_VECTOR(2);
            const uint8_t b = halves ? v1 : BUSTER_TIER0_ROLE_VECTOR(3);
            if (op == CHECK_OP_NEGATIVE)
            {
                // The floats' sign bit in every element, XORed in.
                records[r] = check_vector_lower_record(X86_FORM_MOV_R32_IMM32, 0, 0, s0);
                records[r].immediate = (int32_t)0x80000000u;
                records[r + 1] = check_vector_lower_record(BUSTER_CHECK_VECTOR_FORMS(VPBROADCASTD_X_R32, VPBROADCASTD_Y_R32, VPBROADCASTD_Z_R32).forms[f], b, 0, s0);
                records[r + 2] = check_vector_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORD).forms[f], a, a, b);
                r += 3;
            }
            else if (op == CHECK_OP_DIVIDE && bytes == 4)
            {
                // The two elements' floats alone, under k1, the others zeroed: their 0/0 would raise FE_INVALID.
                records[r] = check_vector_lower_record(X86_FORM_MOV_R32_IMM32, 0, 0, s0);
                records[r].immediate = 3;
                records[r + 1] = check_vector_lower_record(X86_FORM_KMOVW_K_R32, 1, 0, s0);
                records[r + 2] = check_vector_lower_record(floats.forms[f], a, a, b);
                records[r + 2].decorations = 1 | BUSTER_X86_DECORATION_ZEROING;
                r += 3;
            }
            else
            {
                records[r] = check_vector_lower_record(floats.forms[f], a, a, b);
                r += 1;
            }
            records[r] = check_vector_lower_record(narrow[f], v0, 0, a);
            row->record_count = (uint8_t)(r + 1);
            row->operand_count = (uint8_t)operands;
            for (uint32_t j = 0; j < operands; j += 1)
            {
                row->operand_classes[j] = halves ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = halves ? 0 : (uint8_t)bytes;
            }
            row->result_class = BUSTER_TYPED_CLASS_VECTOR;
            row->result_bytes = (uint8_t)(halves ? 32 : bytes);
        }
        if (halves)
        {
            rows[2].records[0] = check_vector_lower_record(X86_FORM_VINSERTF64X4_Z_Z_YM256_IMM8, v0, v0, v1);
            rows[2].records[0].immediate = 1;
            rows[2].record_count = 1;
            rows[2].operand_count = 2;
            rows[2].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            rows[2].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            rows[2].operand_bytes[0] = 32;
            rows[2].operand_bytes[1] = 32;
            rows[2].result_class = BUSTER_TYPED_CLASS_VECTOR;
            rows[2].result_bytes = 64;
            *count = 3;
        }
        needed = BUSTER_X86_FEATURE(AVX512BF16);
    }
    if (reason == NULL && !excess && op != BUSTER_CHECK_VECTOR_SPLAT)
    {
        rows[0].operand_count = (uint8_t)operands;
        for (uint32_t j = 0; j < operands; j += 1)
        {
            rows[0].operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
            rows[0].operand_bytes[j] = (uint8_t)bytes;
        }
    }
    if (reason == NULL && (target->features & needed) != needed)
    {
        reason = (needed & BUSTER_X86_FEATURE(AVX512BF16)) != 0 ? "vector arithmetic of bf16 elements without AVX512-BF16" :
                                                                  "vector operations on 8- and 16-bit elements without AVX-512BW";
    }
    if (reason == NULL && (target->extensions & extensions) != extensions)
    {
        reason = "vector arithmetic of bf16 elements without AVX10.2";
    }
    return reason;
}
