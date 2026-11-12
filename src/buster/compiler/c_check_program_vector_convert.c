// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __builtin_convertvector's rows (#136, #148, #775), the integer elementwise operations' forms and the vector shapes they
// read, apart from the other generic vector builtins' rows (c_check_program_vector_builtins.c, which includes this first)
// so that the IR decoder's tests (ir_simd_typed_test.c, tier1_masks_test.c: #1840) check the decoder against the real
// producer in the backend unit, which compiles c_check_program_vectors.c and c_check_program_x86.c without the rest of
// the lowering (docs/commands/build.md, "Unity test dependencies"). Included after c_check_program_x86.c, whose record helpers it
// takes; it leaves BUSTER_CHECK_BASE_UNARY and BUSTER_CHECK_BASE_PACKED defined for its includer to undefine.

// A vector type as these rows read it: its lanes and bytes, its elements' bytes (1, 2, 4 or 8, or 0 for any other
// size), whether they are integers (bool's aren't) and signed, and whether they are floating ones, and of those bf16's.
typedef struct CheckVectorShape CheckVectorShape;
struct CheckVectorShape
{
    uint32_t lanes;
    uint32_t bytes;
    uint8_t element;
    bool integer;
    bool is_signed;
    bool floating;
    bool bfloat;
};

static CheckVectorShape check_vector_builtin_shape(const TypeTable* types, TypeId vector)
{
    const TypeId plain = type_unqualified(type_strip_aligned(types, vector));
    const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, plain)));
    const uint64_t size = type_size(types, element);
    uint32_t bits = 0;
    bool sign = false;
    const bool integer = element != BUSTER_TYPE_BOOL && check_action_integer_type(types, element, &bits, &sign) && bits == size * 8;
    const uint8_t bytes = size == 1 || size == 2 || size == 4 || size == 8 ? (uint8_t)size : 0;
    const CheckVectorShape result = {
        .lanes = check_builtin_vector_lanes(types, plain),
        .bytes = (uint32_t)type_size(types, plain),
        .element = bytes,
        .integer = integer,
        .is_signed = sign,
        .floating = bytes != 0 && type_kind(types, element) == BUSTER_TYPE_KIND_FLOATING,
        .bfloat = element == BUSTER_TYPE_BF16,
    };
    return result;
}

// A width's place among a family's forms: 16, 32 and 64 bytes, and 16's for a vector of 4 or 8 bytes, in an XMM
// register whose bytes past it Tier 0 zeroes.
static uint32_t check_vector_builtin_width(uint32_t bytes)
{
    return bytes <= 16 ? 0 : bytes == 32 ? 1 : 2;
}

// A one-operand family's forms (dest, source: reg, rm): the EVEX ones at 16 and 32 bytes where VEX has some too.
#define BUSTER_CHECK_VECTOR_UNARY_EVEX(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128_EVEX, name##_Y_YM256_EVEX, name##_Z_ZM512)
#define BUSTER_CHECK_VECTOR_UNARY_PACKED(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128, name##_Y_YM256, name##_Z_ZM512)

// An element's bytes' place in the four-entry tables below: 1, 2, 4 and 8.
static uint32_t check_vector_builtin_element(uint32_t bytes)
{
    return bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
}

// The forms of an integer elementwise operation at an element's bytes, two operands' (dest, first, second: reg, vvvv,
// rm) or one's (dest, source: reg, rm): forms 0 for none. `features` gets what they need past AVX-512F and AVX-512VL.
static CheckVectorForms check_vector_elementwise_forms(CBuiltinId builtin, CheckVectorShape shape, uint64_t* features)
{
    const uint32_t e = check_vector_builtin_element(shape.element);
    CheckVectorForms forms = { { 0, 0, 0 } };
    uint64_t needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    if (builtin == CBUILTIN_ELEMENTWISE_MAX || builtin == CBUILTIN_ELEMENTWISE_MIN || builtin == CBUILTIN_REDUCE_MAX || builtin == CBUILTIN_REDUCE_MIN)
    {
        const bool max = builtin == CBUILTIN_ELEMENTWISE_MAX || builtin == CBUILTIN_REDUCE_MAX;
        static const CheckVectorForms signed_max[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMAXSB), BUSTER_CHECK_VECTOR_EVEX(VPMAXSW), BUSTER_CHECK_VECTOR_EVEX(VPMAXSD),
                                                        BUSTER_CHECK_VECTOR_PACKED(VPMAXSQ) };
        static const CheckVectorForms unsigned_max[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMAXUB), BUSTER_CHECK_VECTOR_EVEX(VPMAXUW), BUSTER_CHECK_VECTOR_EVEX(VPMAXUD),
                                                          BUSTER_CHECK_VECTOR_PACKED(VPMAXUQ) };
        static const CheckVectorForms signed_min[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMINSB), BUSTER_CHECK_VECTOR_EVEX(VPMINSW), BUSTER_CHECK_VECTOR_EVEX(VPMINSD),
                                                        BUSTER_CHECK_VECTOR_PACKED(VPMINSQ) };
        static const CheckVectorForms unsigned_min[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMINUB), BUSTER_CHECK_VECTOR_EVEX(VPMINUW), BUSTER_CHECK_VECTOR_EVEX(VPMINUD),
                                                          BUSTER_CHECK_VECTOR_PACKED(VPMINUQ) };
        forms = max ? (shape.is_signed ? signed_max[e] : unsigned_max[e]) : (shape.is_signed ? signed_min[e] : unsigned_min[e]);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ADD_SAT || builtin == CBUILTIN_ELEMENTWISE_SUB_SAT)
    {
        // AVX-512 saturates 8- and 16-bit elements alone.
        const bool add = builtin == CBUILTIN_ELEMENTWISE_ADD_SAT;
        static const CheckVectorForms saturating[2][2][2] = {
            { { BUSTER_CHECK_VECTOR_EVEX(VPADDSB), BUSTER_CHECK_VECTOR_EVEX(VPADDSW) }, { BUSTER_CHECK_VECTOR_EVEX(VPADDUSB), BUSTER_CHECK_VECTOR_EVEX(VPADDUSW) } },
            { { BUSTER_CHECK_VECTOR_EVEX(VPSUBSB), BUSTER_CHECK_VECTOR_EVEX(VPSUBSW) }, { BUSTER_CHECK_VECTOR_EVEX(VPSUBUSB), BUSTER_CHECK_VECTOR_EVEX(VPSUBUSW) } },
        };
        forms = shape.element <= 2 ? saturating[add ? 0 : 1][shape.is_signed ? 0 : 1][e] : forms;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ABS)
    {
        static const CheckVectorForms abs[4] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSB), BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSW), BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSD),
                                                 BUSTER_CHECK_VECTOR_UNARY_PACKED(VPABSQ) };
        forms = abs[e];
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_POPCOUNT)
    {
        static const CheckVectorForms counts[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTB), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTW),
                                                    BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTD), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTQ) };
        forms = counts[e];
        needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BITALG) : BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CLZG)
    {
        // AVX-512CD counts 32- and 64-bit elements' leading zeros alone.
        forms = shape.element == 4 ? BUSTER_CHECK_VECTOR_UNARY_PACKED(VPLZCNTD) : shape.element == 8 ? BUSTER_CHECK_VECTOR_UNARY_PACKED(VPLZCNTQ) : forms;
        needed = BUSTER_X86_FEATURE(AVX512CD);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR)
    {
        // AVX-512 VBMI2 shifts pairs of 16-, 32- and 64-bit elements alone.
        const bool left = builtin == CBUILTIN_ELEMENTWISE_FSHL;
        static const CheckVectorForms shifts[2][3] = {
            { BUSTER_CHECK_VECTOR_PACKED(VPSHLDVW), BUSTER_CHECK_VECTOR_PACKED(VPSHLDVD), BUSTER_CHECK_VECTOR_PACKED(VPSHLDVQ) },
            { BUSTER_CHECK_VECTOR_PACKED(VPSHRDVW), BUSTER_CHECK_VECTOR_PACKED(VPSHRDVD), BUSTER_CHECK_VECTOR_PACKED(VPSHRDVQ) },
        };
        forms = shape.element >= 2 ? shifts[left ? 0 : 1][e - 1] : forms;
        needed = BUSTER_X86_FEATURE(AVX512VBMI2);
    }
    else if (builtin == CBUILTIN_REDUCE_ADD)
    {
        static const CheckVectorForms add[4] = { BUSTER_CHECK_VECTOR_EVEX(VPADDB), BUSTER_CHECK_VECTOR_EVEX(VPADDW), BUSTER_CHECK_VECTOR_EVEX(VPADDD),
                                                 BUSTER_CHECK_VECTOR_EVEX(VPADDQ) };
        forms = add[e];
    }
    else if (builtin == CBUILTIN_REDUCE_MUL)
    {
        // AVX-512 multiplies 16-, 32- and 64-bit elements alone, the last with AVX-512DQ.
        forms = shape.element == 2 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLW) : shape.element == 4 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLD) :
                shape.element == 8 ? BUSTER_CHECK_VECTOR_PACKED(VPMULLQ) : forms;
        needed = shape.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : shape.element == 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
    }
    else if (builtin == CBUILTIN_REDUCE_AND || builtin == CBUILTIN_REDUCE_OR || builtin == CBUILTIN_REDUCE_XOR)
    {
        // Every element's bits at once: the 64-bit forms whatever the elements.
        forms = builtin == CBUILTIN_REDUCE_AND ? BUSTER_CHECK_VECTOR_PACKED(VPANDQ) : builtin == CBUILTIN_REDUCE_OR ? BUSTER_CHECK_VECTOR_PACKED(VPORQ) :
                                                                                                                  BUSTER_CHECK_VECTOR_PACKED(VPXORQ);
        needed = 0;
    }
    *features = needed;
    return forms;
}

// The reason a row's features refuse it, by the builtin: what the target hasn't of `needed`.
static const char* check_vector_builtin_feature_reason(CBuiltinId builtin, uint64_t needed, uint64_t target)
{
    const uint64_t missing = needed & ~target;
    const char* reason = NULL;
    if ((missing & BUSTER_X86_FEATURE(AVX512BITALG)) != 0)
    {
        reason = "vector population counts of 8- and 16-bit elements without AVX-512 BITALG";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512VPOPCNTDQ)) != 0)
    {
        reason = "vector population counts of 32- and 64-bit elements without AVX-512 VPOPCNTDQ";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512CD)) != 0)
    {
        reason = "vector leading zero counts without AVX-512CD";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512VBMI2)) != 0)
    {
        reason = "vector funnel shifts without AVX-512 VBMI2";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512DQ)) != 0)
    {
        reason = "vector multiplication of 64-bit elements without AVX-512DQ";
    }
    else if (missing != 0)
    {
        reason = builtin >= CBUILTIN_REDUCE_ADD ? "vector reductions of 8- and 16-bit elements without AVX-512BW" :
                                                  "elementwise vector builtins of 8- and 16-bit elements without AVX-512BW";
    }
    return reason;
}

// The most rows a reduction takes: a row's 8 records hold three halvings and the element's move.
#define BUSTER_CHECK_VECTOR_REDUCE_ROWS 2u

// The rows of an integer __builtin_reduce_* function of a vector into its element: halves combined by the operation
// down to one element (the high 32 bytes of 64 by VEXTRACTI64X4, the high 16 of 32 by VEXTRACTI32X4, then within 16
// bytes the high 8 by VPSHUFD 0x4e, the high 4 by VPSHUFD 0xb1, the high 2 by VPSRLD 16 and the high byte by VPSRLW 8,
// each beside the low part in v1), then that element in the general register (VMOVQ, VMOVD, VPEXTRW or VPEXTRB). A
// halving is two records. Byte product reductions first widen to words (pairing widened halves for 64-byte inputs).
// Where a row's eight records aren't enough, split between complete halvings and carry the low live vector bytes
// into the second row, at least 16. `count` gets the rows' number. By the vector's shape, so that the IR decoder's tests
// (#1840, ir_simd_typed_reduce) read the same rows.
static const char* check_vector_reduce_shape_rows(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape,
                                                  Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS], uint32_t* count)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(rows, 0, BUSTER_CHECK_VECTOR_REDUCE_ROWS * sizeof(*rows));
    *count = 0;
    if (!shape.integer)
    {
        reason = "vector reductions of floating elements";
    }
    else if (shape.element == 0)
    {
        reason = "vector reductions of elements of this size";
    }
    else
    {
        CheckVectorShape operation = shape;
        X86Instruction records[2 * BUSTER_TIER0_OPERATION_RECORDS];
        uint32_t r = 0;
        if (builtin == CBUILTIN_REDUCE_MUL && shape.element == 1)
        {
            // The low byte of a product is unchanged by zero-extending its factors and keeping intermediate products
            // modulo 2^16. Widen first, then reuse the packed-word reduction. A 64-byte input has two widened halves:
            // multiply them pairwise before reducing, keeping the live vector at 64 bytes and the plan at two rows.
            operation.element = 2;
            operation.bytes = shape.bytes < 64 ? 2 * shape.bytes : 64;
            if (shape.bytes == 64)
            {
                records[r] = check_vector_lower_record(X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, v0, 0, v1);
                records[r++].immediate = 1;
            }
            const uint32_t widen = shape.bytes <= 8 ? X86_FORM_VPMOVZXBW_X_XM64_EVEX : shape.bytes == 16 ? X86_FORM_VPMOVZXBW_Y_XM128_EVEX :
                                                                                                    X86_FORM_VPMOVZXBW_Z_YM256;
            records[r++] = check_vector_lower_record(widen, v0, 0, v0);
            if (shape.bytes == 64)
            {
                records[r++] = check_vector_lower_record(widen, v1, 0, v1);
                records[r++] = check_vector_lower_record(X86_FORM_VPMULLW_Z_Z_ZM512, v0, v0, v1);
            }
        }
        const uint32_t prefix = r;
        const CheckVectorForms forms = check_vector_elementwise_forms(builtin, operation, &needed);
        uint32_t span = operation.bytes;
        while (span > operation.element && forms.forms[0] != 0)
        {
            span /= 2;
            const uint32_t at = span >= 16 ? span : 16;
            if (span >= 32)
            {
                records[r] = check_vector_lower_record(X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, v0, 0, v1);
            }
            else if (span == 16)
            {
                records[r] = check_vector_lower_record(X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, v0, 0, v1);
            }
            else if (span >= 4)
            {
                records[r] = check_vector_lower_record(X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX, v1, 0, v0);
            }
            else
            {
                records[r] = check_vector_lower_record(span == 2 ? X86_FORM_VPSRLD_X_XM128_IMM8 : X86_FORM_VPSRLW_X_XM128_IMM8, 0, v1, v0);
            }
            records[r].immediate = span >= 16 ? 1 : span == 8 ? 0x4e : span == 4 ? 0xb1 : span == 2 ? 16 : 8;
            records[r + 1] = check_vector_lower_record(forms.forms[check_vector_builtin_width(at)], v0, v0, v1);
            r += 2;
        }
        const uint8_t result = shape.element == 8 ? BUSTER_TYPED_CLASS_I64 : shape.element == 4 ? BUSTER_TYPED_CLASS_I32 :
                               shape.element == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
        const uint32_t move = shape.element == 8 ? X86_FORM_VMOVQ_RM64_X_EVEX : shape.element == 4 ? X86_FORM_VMOVD_RM32_X_EVEX :
                              shape.element == 2 ? X86_FORM_VPEXTRW_R32_X_IMM8_EVEX : X86_FORM_VPEXTRB_RM32_X_IMM8_EVEX;
        // VPEXTRW's register form names the general register in reg, the others in rm.
        records[r] = shape.element == 2 ? check_vector_lower_record(move, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0) :
                                          check_vector_lower_record(move, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
        r += 1;
        needed |= shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        if (forms.forms[0] == 0)
        {
            reason = builtin == CBUILTIN_REDUCE_MUL ? "vector multiplication of 8-bit elements" : "this vector reduction";
        }
        else if ((target->features & needed) != needed)
        {
            reason = check_vector_builtin_feature_reason(builtin, needed, target->features);
        }
        else
        {
            // Split only between complete halvings: the temporary in v1 is private to its row. A widening prefix can
            // leave an odd record count; it must not move a halving's shuffle into a different row from its multiply.
            const bool single = r <= BUSTER_TIER0_OPERATION_RECORDS;
            const uint32_t taken = single ? r : prefix + 2 * ((BUSTER_TIER0_OPERATION_RECORDS - prefix) / 2);
            const uint32_t reduced = operation.bytes >> ((taken - prefix) / 2);
            const uint32_t left = reduced >= 16 ? reduced : 16;
            memcpy(rows[0].records, records, taken * sizeof(*records));
            rows[0].record_count = (uint8_t)taken;
            rows[0].operand_count = 1;
            rows[0].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            rows[0].operand_bytes[0] = (uint8_t)shape.bytes;
            rows[0].result_class = single ? result : BUSTER_TYPED_CLASS_VECTOR;
            rows[0].result_bytes = single ? 0 : (uint8_t)left;
            *count = 1;
            if (!single)
            {
                memcpy(rows[1].records, records + taken, (r - taken) * sizeof(*records));
                rows[1].record_count = (uint8_t)(r - taken);
                rows[1].operand_count = 1;
                rows[1].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                rows[1].operand_bytes[0] = (uint8_t)left;
                rows[1].result_class = result;
                *count = 2;
            }
        }
    }
    return reason;
}


// The forms of a __builtin_convertvector with floating elements (check_vector_convert_row) by its pair, at the width a
// widening one takes at its result's and any other at its source's, or the reason it has none. `needed` gets what they
// need past AVX-512F and AVX-512VL: DQ's for 64-bit integers. Between half-precision elements and 16-, 32- and 64-bit
// integers or doubles they are AVX512-FP16's, which the target's `extensions` must have.
static const char* check_vector_convert_floating_forms(CheckVectorShape in, CheckVectorShape out, uint64_t extensions, CheckVectorForms* forms, uint64_t* needed)
{
    const char* reason = NULL;
    const bool half_in = in.floating && in.element == 2;
    const bool half_out = out.floating && out.element == 2;
    const bool other_in = (in.integer && in.element >= 2) || (in.floating && in.element == 8);
    const bool other_out = (out.integer && out.element >= 2) || (out.floating && out.element == 8);
    const bool fp16 = (half_in && !in.bfloat && other_out) || (half_out && !out.bfloat && other_in);
    *forms = (CheckVectorForms){ { 0, 0, 0 } };
    *needed = (in.integer && in.element == 8) || (out.integer && out.element == 8) ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
    if (in.floating && out.floating && in.element == out.element && in.bfloat == out.bfloat)
    {
        // One format: the bits are the result.
    }
    else if (fp16 && (extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0)
    {
        reason = "vector conversions between half-precision elements and types other than float without AVX512-FP16";
    }
    else if (fp16)
    {
        // To half-precision elements from 16-, 32- and 64-bit integers by their signedness and from doubles, at the
        // source's width (rounded as MXCSR says, as clang's sitofp, uitofp and fptrunc are); from them to those, the
        // integers truncated, at the result's.
        static const CheckVectorForms to_half[3][3] = {
            { BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTW2PH), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTUW2PH), { { 0, 0, 0 } } },
            { BUSTER_CHECK_VECTOR_FORMS(VCVTDQ2PH_X_XM128, VCVTDQ2PH_X_YM256, VCVTDQ2PH_Y_ZM512),
              BUSTER_CHECK_VECTOR_FORMS(VCVTUDQ2PH_X_XM128, VCVTUDQ2PH_X_YM256, VCVTUDQ2PH_Y_ZM512), { { 0, 0, 0 } } },
            { BUSTER_CHECK_VECTOR_FORMS(VCVTQQ2PH_X_XM128, VCVTQQ2PH_X_YM256, VCVTQQ2PH_X_ZM512),
              BUSTER_CHECK_VECTOR_FORMS(VCVTUQQ2PH_X_XM128, VCVTUQQ2PH_X_YM256, VCVTUQQ2PH_X_ZM512),
              BUSTER_CHECK_VECTOR_FORMS(VCVTPD2PH_X_XM128, VCVTPD2PH_X_YM256, VCVTPD2PH_X_ZM512) },
        };
        static const CheckVectorForms from_half[3][3] = {
            { BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTTPH2W), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTTPH2UW), { { 0, 0, 0 } } },
            { BUSTER_CHECK_VECTOR_FORMS(VCVTTPH2DQ_X_XM64, VCVTTPH2DQ_Y_XM128, VCVTTPH2DQ_Z_YM256),
              BUSTER_CHECK_VECTOR_FORMS(VCVTTPH2UDQ_X_XM64, VCVTTPH2UDQ_Y_XM128, VCVTTPH2UDQ_Z_YM256), { { 0, 0, 0 } } },
            { BUSTER_CHECK_VECTOR_FORMS(VCVTTPH2QQ_X_XM32, VCVTTPH2QQ_Y_XM64, VCVTTPH2QQ_Z_XM128),
              BUSTER_CHECK_VECTOR_FORMS(VCVTTPH2UQQ_X_XM32, VCVTTPH2UQQ_Y_XM64, VCVTTPH2UQQ_Z_XM128),
              BUSTER_CHECK_VECTOR_FORMS(VCVTPH2PD_X_XM32, VCVTPH2PD_Y_XM64, VCVTPH2PD_Z_XM128) },
        };
        const CheckVectorShape other = half_out ? in : out;
        const uint32_t size = other.element == 2 ? 0 : other.element == 4 ? 1 : 2;
        const uint32_t kind = other.floating ? 2 : other.is_signed ? 0 : 1;
        *forms = half_out ? to_half[size][kind] : from_half[size][kind];
    }
    else if ((in.integer && in.element <= 2) || (out.integer && out.element <= 2))
    {
        reason = "vector conversions between half-precision elements and 8-bit integers, or bf16 elements and 8- or 16-bit integers";
    }
    else if ((in.bfloat && !(out.floating && !out.bfloat && out.element == 4)) || out.bfloat)
    {
        reason = "vector conversions to bf16 elements, or of bf16 ones to types other than float";
    }
    else if ((half_in && !(out.floating && out.element == 4)) || (half_out && !(in.floating && in.element == 4)))
    {
        reason = "vector conversions between half-precision elements and types other than float";
    }
    else if (in.integer)
    {
        // From 32- and 64-bit integers by their signedness, to floats (4) and doubles (8).
        static const CheckVectorForms to_floating[2][2][2] = {
            { { BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTDQ2PS), BUSTER_CHECK_VECTOR_FORMS(VCVTDQ2PD_X_XM64_EVEX, VCVTDQ2PD_Y_XM128_EVEX, VCVTDQ2PD_Z_YM256) },
              { BUSTER_CHECK_VECTOR_FORMS(VCVTQQ2PS_X_XM128, VCVTQQ2PS_X_YM256, VCVTQQ2PS_Y_ZM512), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTQQ2PD) } },
            { { BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTUDQ2PS), BUSTER_CHECK_VECTOR_FORMS(VCVTUDQ2PD_X_XM64, VCVTUDQ2PD_Y_XM128, VCVTUDQ2PD_Z_YM256) },
              { BUSTER_CHECK_VECTOR_FORMS(VCVTUQQ2PS_X_XM128, VCVTUQQ2PS_X_YM256, VCVTUQQ2PS_Y_ZM512), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTUQQ2PD) } },
        };
        *forms = to_floating[in.is_signed ? 0 : 1][in.element == 8][out.element == 8];
    }
    else if (out.integer)
    {
        // From floats (4) and doubles (8) to 32- and 64-bit integers by their signedness, truncated.
        static const CheckVectorForms to_integer[2][2][2] = {
            { { BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTTPS2DQ), BUSTER_CHECK_VECTOR_FORMS(VCVTTPS2QQ_X_XM64, VCVTTPS2QQ_Y_XM128, VCVTTPS2QQ_Z_YM256) },
              { BUSTER_CHECK_VECTOR_FORMS(VCVTTPD2DQ_X_XM128_EVEX, VCVTTPD2DQ_X_YM256_EVEX, VCVTTPD2DQ_Y_ZM512), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTTPD2QQ) } },
            { { BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTTPS2UDQ), BUSTER_CHECK_VECTOR_FORMS(VCVTTPS2UQQ_X_XM64, VCVTTPS2UQQ_Y_XM128, VCVTTPS2UQQ_Z_YM256) },
              { BUSTER_CHECK_VECTOR_FORMS(VCVTTPD2UDQ_X_XM128, VCVTTPD2UDQ_X_YM256, VCVTTPD2UDQ_Y_ZM512), BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTTPD2UQQ) } },
        };
        *forms = to_integer[out.is_signed ? 0 : 1][in.element == 8][out.element == 8];
    }
    else if (in.element == 4 && out.element == 8)
    {
        *forms = BUSTER_CHECK_VECTOR_FORMS(VCVTPS2PD_X_XM64_EVEX, VCVTPS2PD_Y_XM128_EVEX, VCVTPS2PD_Z_YM256);
    }
    else if (in.element == 8 && out.element == 4)
    {
        *forms = BUSTER_CHECK_VECTOR_FORMS(VCVTPD2PS_X_XM128_EVEX, VCVTPD2PS_X_YM256_EVEX, VCVTPD2PS_Y_ZM512);
    }
    else if (in.bfloat)
    {
        // The bf16 elements zero-extended to 32 bits, then shifted into the floats' high halves (the caller's second
        // record).
        *forms = BUSTER_CHECK_VECTOR_FORMS(VPMOVZXWD_X_XM64_EVEX, VPMOVZXWD_Y_XM128_EVEX, VPMOVZXWD_Z_YM256);
    }
    else if (half_in)
    {
        *forms = BUSTER_CHECK_VECTOR_FORMS(VCVTPH2PS_X_XM64_EVEX, VCVTPH2PS_Y_XM128_EVEX, VCVTPH2PS_Z_YM256);
    }
    else if (half_out)
    {
        // The form's rm is its destination.
        *forms = BUSTER_CHECK_VECTOR_FORMS(VCVTPS2PH_XM64_X_IMM8_EVEX, VCVTPS2PH_XM128_Y_IMM8_EVEX, VCVTPS2PH_YM256_Z_IMM8);
    }
    else
    {
        reason = "vector conversions between floating elements of these types";
    }
    return reason;
}

// The baseline's row of a __builtin_convertvector whose EVEX row has no SSE or AVX form (#1834): between integers, their
// extension or truncation (check_vector_baseline_resize); from 8- or 16-bit integers to floats or doubles, the extension
// to dwords and then CVTDQ2PS or CVTDQ2PD; from floats or doubles to them, CVTTPS2DQ or CVTTPD2DQ and then the
// truncation of the dwords. Its operand and result are the EVEX row's, its records rewritten; NULL or the reason.
static const char* check_vector_baseline_convert(const X86Target* target, CheckVectorShape in, CheckVectorShape out, Tier0Operation* row)
{
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const uint32_t registers = avx ? 32 : 16;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const bool integers = in.integer && out.integer;
    // The dwords' bytes: those an extension writes, those a truncation reads.
    const uint32_t dwords = integers ? (out.element > in.element ? out.bytes : in.bytes) : 4 * in.lanes;
    const bool from_narrow = in.integer && in.element <= 2 && out.floating && !out.bfloat && out.element >= 4;
    const bool to_narrow = out.integer && out.element <= 2 && in.floating && !in.bfloat && in.element >= 4;
    const char* reason = NULL;
    row->record_count = 0;
    CheckVectorBaseline context = { row, dwords, avx, true };
    if (!integers && !from_narrow && !to_narrow)
    {
        reason = "vector conversions unavailable at this target";
    }
    else if (in.bytes > registers || out.bytes > registers)
    {
        reason = "vector conversions wider than the target's registers";
    }
    else if (integers || from_narrow)
    {
        const uint32_t to = integers ? out.element : 4;
        // An extension's sequence is SSE2's alone, without SSE4.1 (and so AVX): 16 bytes.
        context.valid = to < in.element || dwords <= 16;
        check_vector_baseline_resize(&context, in.element, to, in.is_signed);
        if (!integers)
        {
            context.bytes = out.bytes;
            const uint32_t form = out.element == 4 ? BUSTER_CHECK_BASE_UNARY(&context, CVTDQ2PS, X_XM128, Y_YM256) :
                                                     BUSTER_CHECK_BASE_UNARY(&context, CVTDQ2PD, X_XM64, Y_XM128);
            check_vector_baseline_put(&context, check_vector_lower_record(form, v0, 0, v0));
        }
    }
    else
    {
        context.bytes = in.bytes;
        const uint32_t form = in.element == 4 ? BUSTER_CHECK_BASE_UNARY(&context, CVTTPS2DQ, X_XM128, Y_YM256) :
                                                BUSTER_CHECK_BASE_UNARY(&context, CVTTPD2DQ, X_XM128, X_YM256);
        check_vector_baseline_put(&context, check_vector_lower_record(form, v0, 0, v0));
        context.bytes = dwords;
        check_vector_baseline_resize(&context, 4, out.element, true);
    }
    Tier0Operation baseline;
    if (reason == NULL && !(context.valid && check_vector_baseline_row(target, row, &baseline)))
    {
        reason = integers && out.element < in.element ? "vector truncations of integers unavailable at this target" :
                 integers ? "vector extensions of integers unavailable at this target" : "vector conversions unavailable at this target";
    }
    return reason;
}

// The row of __builtin_convertvector between vectors of the same lanes: the operand's first `result`'s lanes elements of
// `source`'s element type (all of a vector of `source`'s type, or a shuffle's first elements of its operand of
// `operand_bytes`, which the conversion takes in place of the shuffle's narrower vector), each to `result`'s element.
// Between integers, a conversion between elements of one size keeps their bits (no records); to wider ones it extends
// them by the source's signedness (VPMOVSX and VPMOVZX, at the result's width), and to narrower ones it truncates them
// (VPMOV, at the source's width, which must then be the operand's). With floating elements it is the instruction of the
// pair, as clang -O0's code is (check_vector_convert_floating_forms): from 32- and 64-bit integers by their signedness
// (VCVTDQ2PS, VCVTUDQ2PD, VCVTQQ2PS and the rest), to them truncated, as C converts (VCVTTPS2DQ, VCVTTPD2UQQ and the
// rest), between floats and doubles (VCVTPS2PD, VCVTPD2PS), from half-precision elements to floats (VCVTPH2PS) and from
// floats to them (VCVTPS2PH at the current rounding, its imm8 4), and from bf16 elements to floats, whose bits are the
// floats' high halves (VPMOVZXWD, then VPSLLD by 16); between floating elements of one format, none.
static const char* check_vector_convert_row(const TypeTable* types, const X86Target* target, TypeId source, uint32_t operand_bytes, TypeId result, Tier0Operation* row)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, source);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)out.bytes;
    row->operand_count = 1;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)operand_bytes;
    if (in.element == 0 || out.element == 0)
    {
        reason = "vector conversions of elements of this size";
    }
    else if ((!in.integer && !in.floating) || (!out.integer && !out.floating))
    {
        reason = "vector conversions of elements of this type";
    }
    else if (out.element < in.element && operand_bytes != in.bytes)
    {
        reason = "vector conversions of a shuffle's elements to narrower ones";
    }
    else if ((in.integer && in.element <= 2 && out.floating && !out.bfloat && out.element >= 4) ||
             (out.integer && out.element <= 2 && in.floating && !in.bfloat && in.element >= 4))
    {
        // Between 8- or 16-bit integers and floats or doubles, through 32-bit integers, as LLVM converts them: extended
        // by the source's signedness (VPMOVSX, VPMOVZX), whose ints a signed conversion takes exactly, then VCVTDQ2PS
        // or VCVTDQ2PD; or truncated by VCVTTPS2DQ or VCVTTPD2DQ, then to the result's elements (VPMOVDW, VPMOVDB),
        // which C leaves undefined outside their range.
        static const CheckVectorForms extend[2][2] = {
            { BUSTER_CHECK_VECTOR_FORMS(VPMOVSXBD_X_XM32_EVEX, VPMOVSXBD_Y_XM64_EVEX, VPMOVSXBD_Z_XM128),
              BUSTER_CHECK_VECTOR_FORMS(VPMOVSXWD_X_XM64_EVEX, VPMOVSXWD_Y_XM128_EVEX, VPMOVSXWD_Z_YM256) },
            { BUSTER_CHECK_VECTOR_FORMS(VPMOVZXBD_X_XM32_EVEX, VPMOVZXBD_Y_XM64_EVEX, VPMOVZXBD_Z_XM128),
              BUSTER_CHECK_VECTOR_FORMS(VPMOVZXWD_X_XM64_EVEX, VPMOVZXWD_Y_XM128_EVEX, VPMOVZXWD_Z_YM256) },
        };
        static const CheckVectorForms truncate[2] = {
            BUSTER_CHECK_VECTOR_FORMS(VPMOVDB_XM32_X, VPMOVDB_XM64_Y, VPMOVDB_XM128_Z),
            BUSTER_CHECK_VECTOR_FORMS(VPMOVDW_XM64_X, VPMOVDW_XM128_Y, VPMOVDW_YM256_Z),
        };
        const uint32_t ints = check_vector_builtin_width(4 * out.lanes);
        if (in.integer)
        {
            const CheckVectorForms convert = out.element == 4 ? BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTDQ2PS) :
                                                                BUSTER_CHECK_VECTOR_FORMS(VCVTDQ2PD_X_XM64_EVEX, VCVTDQ2PD_Y_XM128_EVEX, VCVTDQ2PD_Z_YM256);
            row->records[0] = check_vector_lower_record(extend[in.is_signed ? 0 : 1][in.element == 2].forms[ints], v0, 0, v0);
            row->records[1] = check_vector_lower_record(convert.forms[check_vector_builtin_width(out.bytes)], v0, 0, v0);
        }
        else
        {
            const CheckVectorForms convert = in.element == 4 ? BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTTPS2DQ) :
                                                               BUSTER_CHECK_VECTOR_FORMS(VCVTTPD2DQ_X_XM128_EVEX, VCVTTPD2DQ_X_YM256_EVEX, VCVTTPD2DQ_Y_ZM512);
            // The truncation's rm is its destination.
            row->records[0] = check_vector_lower_record(convert.forms[check_vector_builtin_width(in.bytes)], v0, 0, v0);
            row->records[1] = check_vector_lower_record(truncate[out.element == 2].forms[ints], v0, 0, v0);
        }
        row->record_count = 2;
    }
    else if (in.floating || out.floating)
    {
        CheckVectorForms forms;
        reason = check_vector_convert_floating_forms(in, out, target->extensions, &forms, &needed);
        const uint32_t form = forms.forms[check_vector_builtin_width(out.element > in.element ? out.bytes : in.bytes)];
        if (reason == NULL && form != 0)
        {
            row->records[0] = check_vector_lower_record(form, v0, 0, v0);
            row->records[0].immediate = in.floating && in.element == 4 && out.element == 2 ? 4 : 0;
            row->record_count = 1;
        }
        // bf16's widening to floats, after its first record: an identity conversion of bf16 elements has neither.
        if (reason == NULL && form != 0 && in.bfloat)
        {
            static const CheckVectorForms shifts = BUSTER_CHECK_VECTOR_FORMS(VPSLLD_X_XM128_IMM8, VPSLLD_Y_YM256_IMM8, VPSLLD_Z_ZM512_IMM8);
            row->records[1] = check_vector_lower_record(shifts.forms[check_vector_builtin_width(out.bytes)], 0, v0, v0);
            row->records[1].immediate = 16;
            row->record_count = 2;
        }
        if (reason == NULL && (target->features & needed) != needed)
        {
            reason = "vector conversions between 64-bit integers and floating elements without AVX-512DQ";
        }
    }
    else if (out.element > in.element)
    {
        // By the source's bytes and the result's: 1 to 2, 4 and 8, 2 to 4 and 8, 4 to 8; signed first.
        static const CheckVectorForms extend[2][6] = {
            {
                { { X86_FORM_VPMOVSXBW_X_XM64_EVEX, X86_FORM_VPMOVSXBW_Y_XM128_EVEX, X86_FORM_VPMOVSXBW_Z_YM256 } },
                { { X86_FORM_VPMOVSXBD_X_XM32_EVEX, X86_FORM_VPMOVSXBD_Y_XM64_EVEX, X86_FORM_VPMOVSXBD_Z_XM128 } },
                { { X86_FORM_VPMOVSXBQ_X_XM16_EVEX, X86_FORM_VPMOVSXBQ_Y_XM32_EVEX, X86_FORM_VPMOVSXBQ_Z_XM64 } },
                { { X86_FORM_VPMOVSXWD_X_XM64_EVEX, X86_FORM_VPMOVSXWD_Y_XM128_EVEX, X86_FORM_VPMOVSXWD_Z_YM256 } },
                { { X86_FORM_VPMOVSXWQ_X_XM32_EVEX, X86_FORM_VPMOVSXWQ_Y_XM64_EVEX, X86_FORM_VPMOVSXWQ_Z_XM128 } },
                { { X86_FORM_VPMOVSXDQ_X_XM64_EVEX, X86_FORM_VPMOVSXDQ_Y_XM128_EVEX, X86_FORM_VPMOVSXDQ_Z_YM256 } },
            },
            {
                { { X86_FORM_VPMOVZXBW_X_XM64_EVEX, X86_FORM_VPMOVZXBW_Y_XM128_EVEX, X86_FORM_VPMOVZXBW_Z_YM256 } },
                { { X86_FORM_VPMOVZXBD_X_XM32_EVEX, X86_FORM_VPMOVZXBD_Y_XM64_EVEX, X86_FORM_VPMOVZXBD_Z_XM128 } },
                { { X86_FORM_VPMOVZXBQ_X_XM16_EVEX, X86_FORM_VPMOVZXBQ_Y_XM32_EVEX, X86_FORM_VPMOVZXBQ_Z_XM64 } },
                { { X86_FORM_VPMOVZXWD_X_XM64_EVEX, X86_FORM_VPMOVZXWD_Y_XM128_EVEX, X86_FORM_VPMOVZXWD_Z_YM256 } },
                { { X86_FORM_VPMOVZXWQ_X_XM32_EVEX, X86_FORM_VPMOVZXWQ_Y_XM64_EVEX, X86_FORM_VPMOVZXWQ_Z_XM128 } },
                { { X86_FORM_VPMOVZXDQ_X_XM64_EVEX, X86_FORM_VPMOVZXDQ_Y_XM128_EVEX, X86_FORM_VPMOVZXDQ_Z_YM256 } },
            },
        };
        const uint32_t pair = in.element == 1 ? (out.element == 2 ? 0 : out.element == 4 ? 1 : 2) : in.element == 2 ? (out.element == 4 ? 3 : 4) : 5;
        row->records[0] = check_vector_lower_record(extend[in.is_signed ? 0 : 1][pair].forms[check_vector_builtin_width(out.bytes)], v0, 0, v0);
        row->record_count = 1;
        needed = pair == 0 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    else if (out.element < in.element)
    {
        // By the source's bytes and the result's: 2 to 1, 4 to 1 and 2, 8 to 1, 2 and 4. The form's rm is its
        // destination.
        static const CheckVectorForms truncate[6] = {
            { { X86_FORM_VPMOVWB_XM64_X, X86_FORM_VPMOVWB_XM128_Y, X86_FORM_VPMOVWB_YM256_Z } },
            { { X86_FORM_VPMOVDB_XM32_X, X86_FORM_VPMOVDB_XM64_Y, X86_FORM_VPMOVDB_XM128_Z } },
            { { X86_FORM_VPMOVDW_XM64_X, X86_FORM_VPMOVDW_XM128_Y, X86_FORM_VPMOVDW_YM256_Z } },
            { { X86_FORM_VPMOVQB_XM16_X, X86_FORM_VPMOVQB_XM32_Y, X86_FORM_VPMOVQB_XM64_Z } },
            { { X86_FORM_VPMOVQW_XM32_X, X86_FORM_VPMOVQW_XM64_Y, X86_FORM_VPMOVQW_XM128_Z } },
            { { X86_FORM_VPMOVQD_XM64_X, X86_FORM_VPMOVQD_XM128_Y, X86_FORM_VPMOVQD_YM256_Z } },
        };
        const uint32_t pair = in.element == 2 ? 0 : in.element == 4 ? (out.element == 1 ? 1 : 2) : out.element == 1 ? 3 : out.element == 2 ? 4 : 5;
        row->records[0] = check_vector_lower_record(truncate[pair].forms[check_vector_builtin_width(in.bytes)], v0, 0, v0);
        row->record_count = 1;
        needed = pair == 0 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    const bool baseline = (target->features & evex) != evex;
    Tier0Operation forms;
    if (reason == NULL && row->record_count != 0 && baseline && !check_vector_baseline_row(target, row, &forms))
    {
        // Without AVX-512, SSE's or AVX's form of the conversion (#775), which the lowering takes in the EVEX one's place
        // (check_lower_operation_index), or where it has none (the integers' truncations, their extensions without SSE4.1)
        // the baseline's sequence (check_vector_baseline_convert).
        reason = check_vector_baseline_convert(target, in, out, row);
    }
    else if (reason == NULL && !baseline && (target->features & needed) != needed)
    {
        reason = "vector conversions of 8-bit elements to 16-bit ones without AVX-512BW";
    }
    return reason;
}
