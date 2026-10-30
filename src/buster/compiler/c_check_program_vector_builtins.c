// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's generic vector builtins as Tier 0's machine operations (#136, #148): the rows (Tier0Operation) of
// __builtin_shufflevector, __builtin_convertvector, the __builtin_elementwise_* functions, the __builtin_reduce_*
// functions and __builtin_nontemporal_load and _store at their vectors' types, as clang -O0's code computes them, an
// instruction per element type where AVX-512 has one. A row's forms are EVEX ones of AVX-512F and AVX-512VL, which the
// lowering requires of a function with vectors (check_lower_vector_target), and those of the extension each row names:
// AVX-512BW's for 8- and 16-bit elements, DQ's conversions of 64-bit integers, VBMI's byte permutes, BITALG's and
// VPOPCNTDQ's counts, CD's leading zero counts, VBMI2's funnel shifts, GFNI's bit reversal and AVX512-BMM's byte bit
// reversal; and only forms x86_semantics.c covers (tier0_operation_form_valid). Byte products reduce through widened
// words, keeping their final low byte. Floating elements take conversions between floats, doubles and integers of 32
// and 64 bits, from half-precision and bf16 elements to floats and from floats to half-precision ones, the square roots
// and fused multiply-adds of floats and doubles, vectors and scalars (check_vector_scalar_row), and the functions clang's
// code computes by instructions, of vectors and scalars (check_vector_floating_row, check_vector_floating_scalar_row:
// roundings, canonicalize, copysign, the maximum and minimum functions; check_vector_ldexp_row) and the floating
// reductions (check_vector_reduce_floating_rows); the C library's functions (sin, pow and the rest) are the lowering's
// calls (check_lower_vector_calls). What the rows don't take is refused by name: the other floating functions of
// half-precision and bf16 elements, leading zero counts of 8- and 16-bit elements, 8-bit funnel shifts, saturating
// arithmetic of 32- and 64-bit elements and carry-less products. Included by c_check_program_lower.c after
// c_check_program_x86.c, whose record helpers it takes.

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

// An element's bytes' place in the four-entry tables below: 1, 2, 4 and 8.
static uint32_t check_vector_builtin_element(uint32_t bytes)
{
    return bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
}

// A one-operand family's forms (dest, source: reg, rm): the EVEX ones at 16 and 32 bytes where VEX has some too.
#define BUSTER_CHECK_VECTOR_UNARY_EVEX(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128_EVEX, name##_Y_YM256_EVEX, name##_Z_ZM512)
#define BUSTER_CHECK_VECTOR_UNARY_PACKED(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128, name##_Y_YM256, name##_Z_ZM512)

// The most elements a shuffle's index vector has (64 bytes of 1-byte elements), and an index of no element (clang's -1:
// the result's element is undefined).
#define BUSTER_CHECK_VECTOR_SHUFFLE_LANES 64u
#define BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED UINT32_MAX

// A shuffle's plan beside its row: whether the row reads its second vector, b, and whether it permutes, which takes
// the index vector `constant` (its bytes `constant_bytes`, 16, 32 or 64, the first element's first) as its last operand.
typedef struct CheckVectorShuffle CheckVectorShuffle;
struct CheckVectorShuffle
{
    uint64_t constant[8];
    uint32_t constant_bytes;
    // A narrow shuffle's second vector, a constant of 16 bytes (check_vector_narrow_shuffle_row).
    uint64_t second[8];
    bool reads_b;
    bool permutes;
};

// The row of __builtin_shufflevector(a, b, indices...) of vectors of `source`'s type into one of `result`'s, its
// elements `indices` (`result`'s lanes of them): each an element of a (below a's lanes) or of b (from there to twice
// them), or BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED. Its operands are a, then b where the plan reads it or `b_kept` (b has
// effects to evaluate though its elements aren't read), then the index vector where it permutes.
//
// A shuffle whose every defined element is a's own at its place (a prefix, or a's elements past undefined ones: the
// intrinsics' casts) has no records: a's register holds the result, its bytes past a's zero (Tier 0 loads an operand
// so). Any other is one VPERMT2 at the wider of the two vectors' widths (F bytes, T elements): a is its first table and
// its destination, b its second (or a again where the shuffle doesn't read b), and the index vector selects element i
// of a as i and element i of b as T + i, an undefined element as 0.
static const char* check_vector_shuffle_row(const TypeTable* types, const X86Target* target, TypeId source, TypeId result, const uint32_t* indices, bool b_kept,
                                            Tier0Operation* row, CheckVectorShuffle* plan)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, source);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    bool identity = true;
    bool reads_b = false;
    for (uint32_t i = 0; i < out.lanes && i < BUSTER_CHECK_VECTOR_SHUFFLE_LANES; i += 1)
    {
        const uint32_t k = indices[i];
        identity &= k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED || (k == i && k < in.lanes);
        reads_b |= k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= in.lanes;
    }
    memset(row, 0, sizeof(*row));
    *plan = (CheckVectorShuffle){ .reads_b = reads_b, .permutes = !identity };
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)out.bytes;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)in.bytes;
    uint32_t operands = 1;
    if (reads_b || b_kept)
    {
        row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[1] = (uint8_t)in.bytes;
        operands = 2;
    }
    if (in.element == 0 || out.lanes > BUSTER_CHECK_VECTOR_SHUFFLE_LANES || in.lanes == 0)
    {
        reason = "vector shuffles of elements of this size";
    }
    else if (!identity)
    {
        // The wider vector's register: an XMM one for vectors of 4 and 8 bytes.
        const uint32_t wider = in.bytes > out.bytes ? in.bytes : out.bytes;
        const uint32_t bytes = wider > 16 ? wider : 16;
        const uint32_t table = bytes / in.element;
        uint8_t constant[64] = { 0 };
        for (uint32_t i = 0; i < out.lanes; i += 1)
        {
            const uint32_t k = indices[i];
            const uint64_t index = k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED ? 0 : k < in.lanes ? k : k - in.lanes + table;
            // Little-endian: an element's index in its low byte, the others' zero (VPERMT2 reads its low bits alone).
            constant[i * in.element] = (uint8_t)index;
        }
        memcpy(plan->constant, constant, sizeof(constant));
        plan->constant_bytes = bytes;
        static const CheckVectorForms permutes[4] = {
            { { X86_FORM_VPERMT2B_X_X_XM128, X86_FORM_VPERMT2B_Y_Y_YM256, X86_FORM_VPERMT2B_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2W_X_X_XM128, X86_FORM_VPERMT2W_Y_Y_YM256, X86_FORM_VPERMT2W_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2D_X_X_XM128, X86_FORM_VPERMT2D_Y_Y_YM256, X86_FORM_VPERMT2D_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2Q_X_X_XM128, X86_FORM_VPERMT2Q_Y_Y_YM256, X86_FORM_VPERMT2Q_Z_Z_ZM512 } },
        };
        const uint8_t index_role = BUSTER_TIER0_ROLE_VECTOR(operands);
        row->records[0] = check_vector_lower_record(permutes[check_vector_builtin_element(in.element)].forms[check_vector_builtin_width(bytes)], v0, index_role,
                                                    reads_b ? BUSTER_TIER0_ROLE_VECTOR(1) : v0);
        row->record_count = 1;
        row->operand_classes[operands] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[operands] = (uint8_t)bytes;
        operands += 1;
        const uint64_t needed = in.element == 1 ? BUSTER_X86_FEATURE(AVX512VBMI) : in.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        reason = (target->features & needed) != needed ? (in.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                  "vector shuffles of 16-bit elements without AVX-512BW") :
                                               NULL;
    }
    row->operand_count = (uint8_t)operands;
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
    if (reason == NULL && (target->features & needed) != needed)
    {
        reason = "vector conversions of 8-bit elements to 16-bit ones without AVX-512BW";
    }
    return reason;
}

// The row of a shuffle of a conversion into a vector Tier 0 has no class for (`narrow`, of 2 bytes) with a constant
// vector of its type (`b_bytes`, its bytes, or NULL where the shuffle reads none of its elements): the intrinsics'
// narrowing conversions, which widen the result back with zeros (_mm_cvtepi64_epi8's
// `__builtin_shufflevector(__builtin_convertvector(x, v2qi), (v2qi){0, 0}, 0, 1, 2, 3, 3, ...)`). It truncates
// x into its register's first bytes (check_vector_convert_row's VPMOV), which zeroes the rest: where every result
// element is the conversion's at its place or a zero of the constant's past the conversion's, that is the result. Else
// it permutes the two (VPERMT2, at the result's width or 16 bytes): the conversion's elements are the first table's, the
// constant's the second's. Its operands are x, then where it permutes the constant (`plan->second`, 16 bytes) and the
// index vector.
static const char* check_vector_narrow_shuffle_row(const TypeTable* types, const X86Target* target, TypeId converted, TypeId narrow, TypeId result,
                                                   const uint32_t* indices, const uint8_t* b_bytes, Tier0Operation* row, CheckVectorShuffle* plan)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, converted);
    const CheckVectorShape middle = check_vector_builtin_shape(types, narrow);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = check_vector_convert_row(types, target, converted, in.bytes, narrow, row);
    bool in_place = true;
    bool reads_b = false;
    for (uint32_t i = 0; i < out.lanes && i < BUSTER_CHECK_VECTOR_SHUFFLE_LANES; i += 1)
    {
        const uint32_t k = indices[i];
        bool zero = k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= middle.lanes && b_bytes != NULL;
        for (uint32_t byte = 0; zero && byte < out.element; byte += 1)
        {
            zero = b_bytes[(k - middle.lanes) * out.element + byte] == 0;
        }
        in_place &= k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED || (i < middle.lanes ? k == i : zero);
        reads_b |= k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= middle.lanes;
    }
    *plan = (CheckVectorShuffle){ .reads_b = reads_b && !in_place, .permutes = !in_place };
    if (reason == NULL && (row->record_count != 1 || out.element != middle.element || out.lanes > BUSTER_CHECK_VECTOR_SHUFFLE_LANES ||
                           middle.bytes >= 16 || (reads_b && b_bytes == NULL)))
    {
        reason = "vector types of other than 4, 8, 16, 32 or 64 bytes";
    }
    else if (reason == NULL)
    {
        row->result_bytes = (uint8_t)out.bytes;
        if (!in_place)
        {
            const uint32_t bytes = out.bytes > 16 ? out.bytes : 16;
            const uint32_t table = bytes / out.element;
            uint8_t constant[64] = { 0 };
            for (uint32_t i = 0; i < out.lanes; i += 1)
            {
                const uint32_t k = indices[i];
                constant[i * out.element] = (uint8_t)(k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED ? 0 : k < middle.lanes ? k : k - middle.lanes + table);
            }
            memcpy(plan->constant, constant, sizeof(constant));
            plan->constant_bytes = bytes;
            uint8_t second[64] = { 0 };
            if (b_bytes != NULL)
            {
                memcpy(second, b_bytes, middle.bytes);
            }
            memcpy(plan->second, second, sizeof(second));
            static const CheckVectorForms permutes[4] = {
                { { X86_FORM_VPERMT2B_X_X_XM128, X86_FORM_VPERMT2B_Y_Y_YM256, X86_FORM_VPERMT2B_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2W_X_X_XM128, X86_FORM_VPERMT2W_Y_Y_YM256, X86_FORM_VPERMT2W_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2D_X_X_XM128, X86_FORM_VPERMT2D_Y_Y_YM256, X86_FORM_VPERMT2D_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2Q_X_X_XM128, X86_FORM_VPERMT2Q_Y_Y_YM256, X86_FORM_VPERMT2Q_Z_Z_ZM512 } },
            };
            row->records[1] = check_vector_lower_record(permutes[check_vector_builtin_element(out.element)].forms[check_vector_builtin_width(bytes)], v0,
                                                        BUSTER_TIER0_ROLE_VECTOR(2), BUSTER_TIER0_ROLE_VECTOR(1));
            row->record_count = 2;
            row->operand_count = 3;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = 16;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = (uint8_t)bytes;
            const uint64_t needed = out.element == 1 ? BUSTER_X86_FEATURE(AVX512VBMI) : out.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
            reason = (target->features & needed) != needed ? (out.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                       "vector shuffles of 16-bit elements without AVX-512BW") :
                                                   NULL;
        }
    }
    return reason;
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

// A row's constant operands, past its arguments, whose loads stay outside the operation's eight records, as a shuffle's
// constant index vector's do: each a pattern of `bytes` bytes (1, 2, 4, 8 or 16, its first byte lowest) repeated through
// the operand's bytes, which are the vector's (CheckVectorConstants.vector bytes, at least 16 where `wide`), or a scalar
// row's floating constant, its class's bits (4 or 8 bytes). XOP's byte funnel shifts take 7, 0 and, for left shifts,
// -1; the floating functions their sign or magnitude masks, 1.0 and the largest float below 0.5.
#define BUSTER_CHECK_VECTOR_CONSTANTS 3u

typedef struct CheckVectorConstants CheckVectorConstants;
struct CheckVectorConstants
{
    uint8_t patterns[BUSTER_CHECK_VECTOR_CONSTANTS][16];
    uint8_t bytes[BUSTER_CHECK_VECTOR_CONSTANTS];
    uint32_t count;
};

// Adds a constant of a pattern of `bytes` bytes to a row's, as its next operand of `class` (VECTOR of `operand_bytes`,
// or F32 or F64).
static void check_vector_constant_bytes(Tier0Operation* row, CheckVectorConstants* constants, const uint8_t* pattern, uint32_t bytes, uint8_t class, uint32_t operand_bytes)
{
    const uint32_t c = constants->count;
    if (c < BUSTER_CHECK_VECTOR_CONSTANTS && row->operand_count < BUSTER_TIER0_OPERATION_OPERANDS && bytes <= 16)
    {
        memset(constants->patterns[c], 0, sizeof(constants->patterns[c]));
        memcpy(constants->patterns[c], pattern, bytes);
        constants->bytes[c] = (uint8_t)bytes;
        constants->count = c + 1;
        row->operand_classes[row->operand_count] = class;
        row->operand_bytes[row->operand_count] = class == BUSTER_TYPED_CLASS_VECTOR ? (uint8_t)operand_bytes : 0;
        row->operand_count += 1;
    }
}

// The same of a pattern of up to 8 bytes, its value's low ones.
static void check_vector_constant(Tier0Operation* row, CheckVectorConstants* constants, uint64_t value, uint32_t bytes, uint8_t class, uint32_t operand_bytes)
{
    uint8_t pattern[8];
    for (uint32_t b = 0; b < 8; b += 1)
    {
        pattern[b] = (uint8_t)(value >> (8 * b));
    }
    check_vector_constant_bytes(row, constants, pattern, bytes <= 8 ? bytes : 8, class, operand_bytes);
}

// The row of a __builtin_elementwise_* function at a vector type, of `arguments` operands (all of the type, which its
// result is too): of integers, max and min, abs, saturating addition and subtraction, population counts, leading and
// trailing zero counts (clzg's and ctzg's second operands the elements a zero element takes) and funnel shifts; of
// half-precision elements (with AVX512-FP16), floats and doubles, the square roots and the fused multiply-adds at the
// current rounding, as llvm.sqrt's and llvm.fma's are (VSQRTPH/PS/PD of a, VFMADD213PH/PS/PD of a, b and c: a = b * a +
// c); of bf16 elements (with AVX10.2), VSQRTBF16 of a, and the fused multiply-add of b, a and c (b = a * b + c, as clang's
// code has b in ModRM.reg and a in VEX.vvvv), moved to VECTOR(0), whose operands `negated` has the bit of (the lowering
// takes their negations' operands: check_lower_brain_negations) negate the product (a's or b's alone) or the sum (c's) as
// the form does, as clang's instruction selection folds the negations: VFMADD213BF16, VFMSUB213BF16, VFNMADD213BF16 or
// VFNMSUB213BF16. `constants` gets any extra constant operands.
static const char* check_vector_elementwise_row(const TypeTable* types, const X86Target* target, CBuiltinId builtin, TypeId vector, uint32_t arguments, uint32_t negated,
                                                Tier0Operation* row, CheckVectorConstants* constants)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const char* reason = NULL;
    uint64_t needed = 0;
    *constants = (CheckVectorConstants){ 0 };
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = (uint8_t)arguments;
    for (uint32_t j = 0; j < arguments && j < 3; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    if (shape.floating && !shape.bfloat && shape.element >= 2 && (builtin == CBUILTIN_ELEMENTWISE_SQRT || builtin == CBUILTIN_ELEMENTWISE_FMA))
    {
        static const CheckVectorForms roots[3] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VSQRTPH), BUSTER_CHECK_VECTOR_UNARY_EVEX(VSQRTPS),
                                                   BUSTER_CHECK_VECTOR_UNARY_EVEX(VSQRTPD) };
        static const CheckVectorForms products[3] = { BUSTER_CHECK_VECTOR_PACKED(VFMADD213PH), BUSTER_CHECK_VECTOR_EVEX(VFMADD213PS),
                                                      BUSTER_CHECK_VECTOR_EVEX(VFMADD213PD) };
        const uint32_t d = shape.element == 2 ? 0 : shape.element == 4 ? 1 : 2;
        row->records[0] = builtin == CBUILTIN_ELEMENTWISE_SQRT ? check_vector_lower_record(roots[d].forms[width], v0, 0, v0) :
                                                                  check_vector_lower_record(products[d].forms[width], v0, v1, v2);
        row->record_count = 1;
        reason = shape.element == 2 && (target->extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0 ?
                     "elementwise vector builtins of half-precision elements without AVX512-FP16" :
                     NULL;
    }
    else if (shape.bfloat && (builtin == CBUILTIN_ELEMENTWISE_SQRT || builtin == CBUILTIN_ELEMENTWISE_FMA))
    {
        static const CheckVectorForms products[4] = { BUSTER_CHECK_VECTOR_PACKED(VFMADD213BF16), BUSTER_CHECK_VECTOR_PACKED(VFMSUB213BF16),
                                                      BUSTER_CHECK_VECTOR_PACKED(VFNMADD213BF16), BUSTER_CHECK_VECTOR_PACKED(VFNMSUB213BF16) };
        // 1 for a negated sum, 2 for a negated product.
        const uint32_t sign = (negated >> 2 & 1) | ((negated ^ negated >> 1) & 1) << 1;
        if (builtin == CBUILTIN_ELEMENTWISE_SQRT)
        {
            row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_UNARY_PACKED(VSQRTBF16).forms[width], v0, 0, v0);
            row->record_count = 1;
        }
        else
        {
            row->records[0] = check_vector_lower_record(products[sign].forms[width], v1, v0, v2);
            row->records[1] = check_x86_lower_move_result(shape.bytes, v1);
            row->record_count = 2;
        }
        reason = (target->extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0 ? "elementwise vector builtins of bf16 elements without AVX10.2" : NULL;
    }
    else if (shape.bfloat)
    {
        reason = "elementwise vector builtins of bf16 elements";
    }
    else if (!shape.integer)
    {
        reason = "elementwise vector builtins of floating elements";
    }
    else if (shape.element == 0)
    {
        reason = "elementwise vector builtins of elements of this size";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_BITREVERSE && shape.element == 1 && (target->extensions & BUSTER_X86_EXTENSION(AVX512BMM)) != 0)
    {
        row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_UNARY_PACKED(VBITREVB).forms[width], v0, 0, v0);
        row->record_count = 1;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_BITREVERSE)
    {
        // Each element's bytes reversed (VPSHUFB by a constant of their places, within each 16 bytes), then each byte's
        // bits (VGF2P8AFFINEQB by the matrix that reverses them, 0x8040201008040201 in every quadword), as clang's code
        // on a target with GFNI computes llvm.bitreverse. The constants are of 16 bytes at least: the matrix is a
        // quadword's whole, which a vector of 4 bytes is less than.
        const uint32_t constant_bytes = shape.bytes < 16 ? 16 : shape.bytes;
        uint32_t r = 0;
        if (shape.element > 1)
        {
            uint8_t places[16];
            for (uint32_t b = 0; b < 16; b += 1)
            {
                places[b] = (uint8_t)(b / shape.element * shape.element + (shape.element - 1 - b % shape.element));
            }
            static const CheckVectorForms shuffles = BUSTER_CHECK_VECTOR_EVEX(VPSHUFB);
            check_vector_constant_bytes(row, constants, places, 16, BUSTER_TYPED_CLASS_VECTOR, constant_bytes);
            row->records[r++] = check_vector_lower_record(shuffles.forms[width], v0, v0, BUSTER_TIER0_ROLE_VECTOR(row->operand_count - 1));
            needed = BUSTER_X86_FEATURE(AVX512BW);
        }
        static const CheckVectorForms affines = BUSTER_CHECK_VECTOR_FORMS(VGF2P8AFFINEQB_X_X_XM128_IMM8_EVEX, VGF2P8AFFINEQB_Y_Y_YM256_IMM8_EVEX,
                                                                          VGF2P8AFFINEQB_Z_Z_ZM512_IMM8);
        check_vector_constant(row, constants, UINT64_C(0x8040201008040201), 8, BUSTER_TYPED_CLASS_VECTOR, constant_bytes);
        row->records[r++] = check_vector_lower_record(affines.forms[width], v0, v0, BUSTER_TIER0_ROLE_VECTOR(row->operand_count - 1));
        row->record_count = (uint8_t)r;
        reason = (target->features & BUSTER_X86_FEATURE(GFNI)) == 0 ? "vector bit reversal without GFNI" :
                 (target->features & needed) != needed ? "vector bit reversal of elements wider than a byte without AVX-512BW" :
                                                         NULL;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CLMUL)
    {
        // VPCLMULQDQ, the one instruction of carry-less products, is no form x86_semantics.c covers, which Tier 0's rows
        // take alone (tier0_operation_form_valid).
        reason = "vector carry-less multiplication, whose VPCLMULQDQ no Tier 0 row takes";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CTZG)
    {
        // popcount(~x & (x - 1)), as clang's code computes llvm.cttz: each element's trailing zeros, its width for a zero
        // one; with a second operand, its element where the first's is zero (k1, from VPTESTNM, before the count overwrites
        // the first operand), as clzg's select is.
        const uint32_t e = check_vector_builtin_element(shape.element);
        static const CheckVectorForms adds[4] = { BUSTER_CHECK_VECTOR_EVEX(VPADDB), BUSTER_CHECK_VECTOR_EVEX(VPADDW), BUSTER_CHECK_VECTOR_EVEX(VPADDD),
                                                  BUSTER_CHECK_VECTOR_EVEX(VPADDQ) };
        static const CheckVectorForms counts[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTB), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTW),
                                                    BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTD), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTQ) };
        // Zero elements into k1: of 32- and 64-bit ones, VPTESTNM's, then VPBLENDM's select; of 8- and 16-bit ones, VPCMPEQ
        // with zeros' (VPXORD) and a masked VMOVDQU8 or VMOVDQU16, forms x86_semantics.c covers.
        static const CheckVectorForms tests[4] = {
            BUSTER_CHECK_VECTOR_FORMS(VPCMPEQB_K_X_XM128, VPCMPEQB_K_Y_YM256, VPCMPEQB_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPCMPEQW_K_X_XM128, VPCMPEQW_K_Y_YM256, VPCMPEQW_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPTESTNMD_K_X_XM128, VPTESTNMD_K_Y_YM256, VPTESTNMD_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPTESTNMQ_K_X_XM128, VPTESTNMQ_K_Y_YM256, VPTESTNMQ_K_Z_ZM512),
        };
        static const CheckVectorForms blends[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VMOVDQU8), BUSTER_CHECK_VECTOR_UNARY_PACKED(VMOVDQU16),
                                                    BUSTER_CHECK_VECTOR_PACKED(VPBLENDMD), BUSTER_CHECK_VECTOR_PACKED(VPBLENDMQ) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        static const CheckVectorForms zeros = BUSTER_CHECK_VECTOR_PACKED(VPXORD);
        uint32_t r = 0;
        if (arguments == 2 && shape.element <= 2)
        {
            row->records[r++] = check_vector_lower_record(zeros.forms[width], v2, v2, v2);
            row->records[r++] = check_vector_lower_record(tests[e].forms[width], 1, v0, v2);
        }
        else if (arguments == 2)
        {
            row->records[r++] = check_vector_lower_record(tests[e].forms[width], 1, v0, v0);
        }
        // All ones (any inputs' every truth table entry 1), then x - 1, then that and not x (A & ~B, A the destination
        // and B x: 0x30), then its count into v0.
        row->records[r] = check_vector_lower_record(ternary.forms[width], v2, v2, v2);
        row->records[r++].immediate = 0xff;
        row->records[r++] = check_vector_lower_record(adds[e].forms[width], v2, v0, v2);
        row->records[r] = check_vector_lower_record(ternary.forms[width], v2, v0, v0);
        row->records[r++].immediate = 0x30;
        row->records[r++] = check_vector_lower_record(counts[e].forms[width], v0, 0, v2);
        if (arguments == 2)
        {
            // VMOVDQU8's and VMOVDQU16's source is in rm, VPBLENDM's second in rm too (k1's elements from it).
            row->records[r] = shape.element <= 2 ? check_vector_lower_record(blends[e].forms[width], v0, 0, v1) :
                                                   check_vector_lower_record(blends[e].forms[width], v0, v0, v1);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
        needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512BITALG) : BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
        reason = (target->features & needed) == needed ? NULL :
                 shape.element <= 2 ? "vector trailing zero counts of 8- and 16-bit elements without AVX-512 BITALG" :
                                      "vector trailing zero counts of 32- and 64-bit elements without AVX-512 VPOPCNTDQ";
    }
    else if (shape.element == 1 && (builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR))
    {
        if (shape.bytes != 16)
        {
            reason = "vector funnel shifts of 8-bit elements outside 16-byte XOP vectors";
        }
        else if ((target->extensions & BUSTER_X86_EXTENSION(XOP)) == 0)
        {
            reason = "vector funnel shifts of 8-bit elements without XOP";
        }
        else
        {
            const bool left = builtin == CBUILTIN_ELEMENTWISE_FSHL;
            const uint8_t mask = BUSTER_TIER0_ROLE_VECTOR(3);
            const uint8_t zero = BUSTER_TIER0_ROLE_VECTOR(4);
            const uint8_t ones = BUSTER_TIER0_ROLE_VECTOR(5);
            const uint8_t temporary = BUSTER_TIER0_ROLE_VECTOR(6);
            check_vector_constant(row, constants, 7, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            check_vector_constant(row, constants, 0, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            if (left)
            {
                check_vector_constant(row, constants, 255, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            }
            // VPSHLB uses a signed count: positive shifts left, negative shifts right. With s = c & 7, first
            // b >> 1 >> (7-s), then a << s for fshl; a << 1 << (7-s), then b >> s for fshr. Splitting the
            // complementary shift makes it eight at s == 0, which discards that half instead of rotating it.
            if (left)
            {
                row->records[0] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, ones, v1);
                row->records[1] = check_vector_lower_record(X86_FORM_VPANDN_X_X_XM128, temporary, v2, mask);
                row->records[2] = check_vector_lower_record(X86_FORM_VPSUBB_X_X_XM128, temporary, zero, temporary);
                row->records[3] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, temporary, v1);
                row->records[4] = check_vector_lower_record(X86_FORM_VPAND_X_X_XM128, v2, v2, mask);
                row->records[5] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v0, v2, v0);
            }
            else
            {
                row->records[0] = check_vector_lower_record(X86_FORM_VPANDN_X_X_XM128, temporary, v2, mask);
                row->records[1] = check_vector_lower_record(X86_FORM_VPADDB_X_X_XM128, v0, v0, v0);
                row->records[2] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v0, temporary, v0);
                row->records[3] = check_vector_lower_record(X86_FORM_VPAND_X_X_XM128, v2, v2, mask);
                row->records[4] = check_vector_lower_record(X86_FORM_VPSUBB_X_X_XM128, v2, zero, v2);
                row->records[5] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, v2, v1);
            }
            row->records[6] = check_vector_lower_record(X86_FORM_VPOR_X_X_XM128, v0, v0, v1);
            row->record_count = 7;
        }
    }
    else
    {
        const CheckVectorForms forms = check_vector_elementwise_forms(builtin, shape, &needed);
        const uint32_t form = forms.forms[width];
        if (form == 0)
        {
            reason = builtin == CBUILTIN_ELEMENTWISE_ADD_SAT || builtin == CBUILTIN_ELEMENTWISE_SUB_SAT ? "saturating vector arithmetic of 32- and 64-bit elements" :
                     builtin == CBUILTIN_ELEMENTWISE_CLZG ? "vector leading zero counts of 8- and 16-bit elements" :
                     builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR ? "vector funnel shifts of 8-bit elements" :
                                                                                                    "this elementwise vector builtin";
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_ABS || builtin == CBUILTIN_ELEMENTWISE_POPCOUNT || (builtin == CBUILTIN_ELEMENTWISE_CLZG && arguments == 1))
        {
            row->records[0] = check_vector_lower_record(form, v0, 0, v0);
            row->record_count = 1;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CLZG)
        {
            // The count into v2, then the second operand's element where the first's is zero (k1, from VPTESTNM): clang's
            // select of ctlz's result by the element's compare with zero.
            const bool wide = shape.element == 8;
            const CheckVectorForms tests = wide ? BUSTER_CHECK_VECTOR_FORMS(VPTESTNMQ_K_X_XM128, VPTESTNMQ_K_Y_YM256, VPTESTNMQ_K_Z_ZM512) :
                                                  BUSTER_CHECK_VECTOR_FORMS(VPTESTNMD_K_X_XM128, VPTESTNMD_K_Y_YM256, VPTESTNMD_K_Z_ZM512);
            const CheckVectorForms blends = wide ? BUSTER_CHECK_VECTOR_FORMS(VPBLENDMQ_X_X_XM128, VPBLENDMQ_Y_Y_YM256, VPBLENDMQ_Z_Z_ZM512) :
                                                   BUSTER_CHECK_VECTOR_FORMS(VPBLENDMD_X_X_XM128, VPBLENDMD_Y_Y_YM256, VPBLENDMD_Z_Z_ZM512);
            row->records[0] = check_vector_lower_record(form, v2, 0, v0);
            row->records[1] = check_vector_lower_record(tests.forms[width], 1, v0, v0);
            row->records[2] = check_vector_lower_record(blends.forms[width], v0, v2, v1);
            row->records[2].decorations = 1;
            row->record_count = 3;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_FSHL)
        {
            // The high halves of a:b shifted left: VPSHLDV's destination is a, the high one.
            row->records[0] = check_vector_lower_record(form, v0, v1, v2);
            row->record_count = 1;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_FSHR)
        {
            // The low halves of a:b shifted right: VPSHRDV's destination is b, the low one, which then goes to v0.
            row->records[0] = check_vector_lower_record(form, v1, v0, v2);
            row->records[1] = check_x86_lower_move_result(shape.bytes, v1);
            row->record_count = 2;
        }
        else
        {
            row->records[0] = check_vector_lower_record(form, v0, v0, v1);
            row->record_count = 1;
        }
        if (reason == NULL && (target->features & needed) != needed)
        {
            reason = check_vector_builtin_feature_reason(builtin, needed, target->features);
        }
    }
    return reason;
}

// The row of __builtin_elementwise_sqrt or _fma of float or double scalars (`class`, F32 or F64), of `arguments`
// operands of the class: VSQRTSS/SD of the operand, or VFMADD213SS/SD of a, b and c (a = b * a + c), into the first
// operand's register at the current rounding, as llvm.sqrt and llvm.fma compute them. Their EVEX forms are AVX-512F's,
// which a target may lack though the operands are no vectors.
static const char* check_vector_scalar_row(const X86Target* target, CBuiltinId builtin, uint8_t class, uint32_t arguments, Tier0Operation* row)
{
    const bool root = builtin == CBUILTIN_ELEMENTWISE_SQRT && arguments == 1;
    const bool product = builtin == CBUILTIN_ELEMENTWISE_FMA && arguments == 3;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    if (!root && !product)
    {
        reason = "elementwise builtins of scalars";
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0)
    {
        reason = "elementwise builtins of scalars without AVX-512F";
    }
    else
    {
        const bool d = class == BUSTER_TYPED_CLASS_F64;
        const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
        row->records[0] = root ? check_vector_lower_record(d ? X86_FORM_VSQRTSD_X_X_XM64_EVEX : X86_FORM_VSQRTSS_X_X_XM32_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT,
                                                           f0, f0) :
                                 check_vector_lower_record(d ? X86_FORM_VFMADD213SD_X_X_XM64_EVEX : X86_FORM_VFMADD213SS_X_X_XM32_EVEX,
                                                           BUSTER_TIER0_ROLE_FLOATING_RESULT, BUSTER_TIER0_ROLE_FLOATING(1), BUSTER_TIER0_ROLE_FLOATING(2));
        row->record_count = 1;
        row->operand_count = (uint8_t)arguments;
        for (uint32_t j = 0; j < arguments; j += 1)
        {
            row->operand_classes[j] = class;
        }
        row->result_class = class;
    }
    return reason;
}

// Whether a floating elementwise builtin is computed by instructions (check_vector_floating_row and its scalar twin),
// rather than by the C library's functions an element at a time (sin, pow and the rest: check_lower_vector_calls).
static bool check_vector_floating_instructions(CBuiltinId builtin)
{
    const bool result = (builtin >= CBUILTIN_ELEMENTWISE_CEIL && builtin <= CBUILTIN_ELEMENTWISE_CANONICALIZE) ||
                        (builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN && builtin <= CBUILTIN_ELEMENTWISE_MINIMUMNUM);
    return result;
}

// The rounding immediate of VRNDSCALE for a rounding builtin, as llvm.floor ... llvm.roundeven are: bits 1 and 0 the
// rounding mode (to nearest even, down, up, toward zero), bit 2 MXCSR's mode instead, bit 3 no precision exception:
// floor 9, ceil 10, rint 4, nearbyint 12, roundeven 8, and trunc 11, which round's truncation is too.
static uint8_t check_vector_rounding(CBuiltinId builtin)
{
    const uint8_t result = builtin == CBUILTIN_ELEMENTWISE_FLOOR ? 9 : builtin == CBUILTIN_ELEMENTWISE_CEIL ? 10 : builtin == CBUILTIN_ELEMENTWISE_RINT ? 4 :
                           builtin == CBUILTIN_ELEMENTWISE_NEARBYINT ? 12 : builtin == CBUILTIN_ELEMENTWISE_ROUNDEVEN ? 8 : 11;
    return result;
}

// The floating constants of the rows below, as a float's (`d` false) or a double's bits: the sign mask, the magnitude
// mask (all but the sign), the largest value below 0.5 (llvm.round's) and 1.0.
static uint64_t check_vector_floating_sign(bool d)
{
    return d ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
}

static uint64_t check_vector_floating_half(bool d)
{
    return d ? UINT64_C(0x3fdfffffffffffff) : UINT64_C(0x3effffff);
}

static uint64_t check_vector_floating_one(bool d)
{
    return d ? UINT64_C(0x3ff0000000000000) : UINT64_C(0x3f800000);
}

// The floating elementwise builtins clang -O0's code computes by instructions (check_vector_floating_instructions), of a
// vector of floats or doubles, as clang's sequences compute them, operand for operand, so that NaN payloads and zeros'
// signs are clang's: VRNDSCALE for the roundings (check_vector_rounding's immediates); llvm.round's x + copysign(0.5's
// predecessor, x), truncated (VPTERNLOGD 248 of the predecessor, x and the sign mask, VADDP, VRNDSCALE 11);
// canonicalize's x * 1.0 (VMULP); copysign's bits of x's magnitude and y's sign (VPTERNLOGD 226 by the magnitude mask);
// maxnum and minnum, y > x ? y : x (VMAXP or VMINP of y and x), y where x is a NaN (a masked move by VCMPP's unordered
// compare, made first, before the result overwrites x); maximum and minimum, x > y ? x : y with its sign bit cleared
// where x's is clear (maximum's VPTERNLOGD 224 with the magnitude mask) or set where x's is (minimum's VPTERNLOGD 248
// with the sign mask), and x where x is a NaN (the ordered compare's mask moving the rest); maximumnum and minimumnum,
// the same, but y where x is a NaN and x where y is. The constants are operands past the arguments (`constants`).
static const char* check_vector_floating_row(const TypeTable* types, CBuiltinId builtin, TypeId vector, Tier0Operation* row, CheckVectorConstants* constants)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const bool binary = builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *constants = (CheckVectorConstants){ 0 };
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = binary ? 2 : 1;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "elementwise vector builtins of half-precision or bf16 elements";
    }
    else
    {
        const bool d = shape.element == 8;
        const uint64_t sign = check_vector_floating_sign(d);
        static const CheckVectorForms roundings[2] = { BUSTER_CHECK_VECTOR_FORMS(VRNDSCALEPS_X_XM128_IMM8, VRNDSCALEPS_Y_YM256_IMM8, VRNDSCALEPS_Z_ZM512_IMM8),
                                                       BUSTER_CHECK_VECTOR_FORMS(VRNDSCALEPD_X_XM128_IMM8, VRNDSCALEPD_Y_YM256_IMM8, VRNDSCALEPD_Z_ZM512_IMM8) };
        static const CheckVectorForms maxima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMAXPS), BUSTER_CHECK_VECTOR_EVEX(VMAXPD) };
        static const CheckVectorForms minima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMINPS), BUSTER_CHECK_VECTOR_EVEX(VMINPD) };
        static const CheckVectorForms compares[2] = { BUSTER_CHECK_VECTOR_FORMS(VCMPPS_K_X_XM128_IMM8, VCMPPS_K_Y_YM256_IMM8, VCMPPS_K_Z_ZM512_IMM8),
                                                      BUSTER_CHECK_VECTOR_FORMS(VCMPPD_K_X_XM128_IMM8, VCMPPD_K_Y_YM256_IMM8, VCMPPD_K_Z_ZM512_IMM8) };
        static const CheckVectorForms moves[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPS), BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPD) };
        static const CheckVectorForms adds[2] = { BUSTER_CHECK_VECTOR_EVEX(VADDPS), BUSTER_CHECK_VECTOR_EVEX(VADDPD) };
        static const CheckVectorForms products[2] = { BUSTER_CHECK_VECTOR_EVEX(VMULPS), BUSTER_CHECK_VECTOR_EVEX(VMULPD) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        const uint32_t compare = compares[d].forms[width];
        const uint32_t move = moves[d].forms[width];
        uint32_t r = 0;
        if (builtin <= CBUILTIN_ELEMENTWISE_ROUNDEVEN)
        {
            row->records[r] = check_vector_lower_record(roundings[d].forms[width], v0, 0, v0);
            row->records[r++].immediate = check_vector_rounding(builtin);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
        {
            check_vector_constant(row, constants, check_vector_floating_half(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            check_vector_constant(row, constants, sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v1, v0, v2);
            row->records[r++].immediate = 248;
            row->records[r++] = check_vector_lower_record(adds[d].forms[width], v0, v0, v1);
            row->records[r] = check_vector_lower_record(roundings[d].forms[width], v0, 0, v0);
            row->records[r++].immediate = 11;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
        {
            check_vector_constant(row, constants, check_vector_floating_one(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r++] = check_vector_lower_record(products[d].forms[width], v0, v0, v1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
        {
            // x where the mask is set, y elsewhere: (B ? A : C), A x, B the mask, C y.
            check_vector_constant(row, constants, sign - 1, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v0, v2, v1);
            row->records[r++].immediate = 226;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
        {
            const uint32_t operation = (builtin == CBUILTIN_ELEMENTWISE_MAXNUM ? maxima : minima)[d].forms[width];
            row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
            row->records[r++].immediate = 3;
            row->records[r++] = check_vector_lower_record(operation, v0, v1, v0);
            row->records[r] = check_vector_lower_record(move, v0, 0, v1);
            row->records[r++].decorations = 1;
        }
        else
        {
            const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
            const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
            check_vector_constant(row, constants, maximum ? sign - 1 : sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r++] = check_vector_lower_record((maximum ? maxima : minima)[d].forms[width], v3, v0, v1);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v3, v0, v2);
            row->records[r++].immediate = maximum ? 224 : 248;
            if (number)
            {
                row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
                row->records[r++].immediate = 3;
                row->records[r] = check_vector_lower_record(move, v3, 0, v1);
                row->records[r++].decorations = 1;
                row->records[r] = check_vector_lower_record(compare, 1, v1, v1);
                row->records[r++].immediate = 7;
            }
            else
            {
                row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
                row->records[r++].immediate = 7;
            }
            row->records[r] = check_vector_lower_record(move, v0, 0, v3);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
    }
    return reason;
}

// The same builtins (and __builtin_elementwise_ldexp) of float or double scalars (`class`, F32 or F64), its scalar
// instructions' sequence: x FLOATING(0), y FLOATING(1), then the constants, as scalars of the class, and the result
// FLOATING_RESULT, which is x's register; VECTOR(0) a free one. ldexp's exponent is a value of the class already (the
// lowering converts it), which VSCALEFSS or VSCALEFSD takes. Their EVEX forms and opmasks are AVX-512F's.
static const char* check_vector_floating_scalar_row(const X86Target* target, CBuiltinId builtin, uint8_t class, Tier0Operation* row, CheckVectorConstants* constants)
{
    const bool d = class == BUSTER_TYPED_CLASS_F64;
    const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
    const uint8_t f1 = BUSTER_TIER0_ROLE_FLOATING(1);
    const uint8_t f2 = BUSTER_TIER0_ROLE_FLOATING(2);
    const uint8_t fr = BUSTER_TIER0_ROLE_FLOATING_RESULT;
    const uint8_t t = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint64_t sign = check_vector_floating_sign(d);
    const bool binary = builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *constants = (CheckVectorConstants){ 0 };
    row->result_class = class;
    row->operand_count = binary ? 2 : 1;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = class;
    }
    if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0)
    {
        reason = "elementwise builtins of scalars without AVX-512F";
    }
    else
    {
        // A scalar's rounding is VRNDSCALEPS's or VRNDSCALEPD's of a register holding it alone, zeros past it (VPXORD, then
        // VMOVSS or VMOVSD), which round exactly: the scalar forms' result and exceptions (clang's VROUNDSS's), by forms
        // x86_semantics.c covers.
        const uint32_t rounding = d ? X86_FORM_VRNDSCALEPD_X_XM128_IMM8 : X86_FORM_VRNDSCALEPS_X_XM128_IMM8;
        const uint32_t compare = d ? X86_FORM_VCMPSD_K_X_XM64_IMM8 : X86_FORM_VCMPSS_K_X_XM32_IMM8;
        const uint32_t move = d ? X86_FORM_VMOVSD_X_X_X_EVEX : X86_FORM_VMOVSS_X_X_X_EVEX;
        const uint32_t ternary = X86_FORM_VPTERNLOGD_X_X_XM128_IMM8;
        uint32_t r = 0;
        if (builtin <= CBUILTIN_ELEMENTWISE_ROUND)
        {
            uint8_t source = f0;
            if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
            {
                check_vector_constant(row, constants, check_vector_floating_half(d), d ? 8 : 4, class, 0);
                check_vector_constant(row, constants, sign, d ? 8 : 4, class, 0);
                row->records[r] = check_vector_lower_record(ternary, f1, f0, f2);
                row->records[r++].immediate = 248;
                row->records[r++] = check_vector_lower_record(d ? X86_FORM_VADDSD_X_X_XM64_EVEX : X86_FORM_VADDSS_X_X_XM32_EVEX, f1, f0, f1);
                source = f1;
            }
            row->records[r++] = check_vector_lower_record(X86_FORM_VPXORD_X_X_XM128, t, t, t);
            row->records[r++] = check_vector_lower_record(move, t, t, source);
            row->records[r] = check_vector_lower_record(rounding, fr, 0, t);
            row->records[r++].immediate = check_vector_rounding(builtin);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
        {
            check_vector_constant(row, constants, check_vector_floating_one(d), d ? 8 : 4, class, 0);
            row->records[r++] = check_vector_lower_record(d ? X86_FORM_VMULSD_X_X_XM64_EVEX : X86_FORM_VMULSS_X_X_XM32_EVEX, fr, f0, f1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
        {
            check_vector_constant(row, constants, sign - 1, d ? 8 : 4, class, 0);
            row->records[r] = check_vector_lower_record(ternary, fr, f2, f1);
            row->records[r++].immediate = 226;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_LDEXP)
        {
            row->records[r++] = check_vector_lower_record(d ? X86_FORM_VSCALEFSD_X_X_XM64 : X86_FORM_VSCALEFSS_X_X_XM32, fr, f0, f1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
        {
            const uint32_t operation = builtin == CBUILTIN_ELEMENTWISE_MAXNUM ? (d ? X86_FORM_VMAXSD_X_X_XM64_EVEX : X86_FORM_VMAXSS_X_X_XM32_EVEX) :
                                                                                (d ? X86_FORM_VMINSD_X_X_XM64_EVEX : X86_FORM_VMINSS_X_X_XM32_EVEX);
            row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
            row->records[r++].immediate = 3;
            row->records[r++] = check_vector_lower_record(operation, fr, f1, f0);
            row->records[r] = check_vector_lower_record(move, fr, fr, f1);
            row->records[r++].decorations = 1;
        }
        else
        {
            const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
            const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
            const uint32_t operation = maximum ? (d ? X86_FORM_VMAXSD_X_X_XM64_EVEX : X86_FORM_VMAXSS_X_X_XM32_EVEX) :
                                                 (d ? X86_FORM_VMINSD_X_X_XM64_EVEX : X86_FORM_VMINSS_X_X_XM32_EVEX);
            check_vector_constant(row, constants, maximum ? sign - 1 : sign, d ? 8 : 4, class, 0);
            row->records[r++] = check_vector_lower_record(operation, t, f0, f1);
            row->records[r] = check_vector_lower_record(ternary, t, f0, f2);
            row->records[r++].immediate = maximum ? 224 : 248;
            if (number)
            {
                row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
                row->records[r++].immediate = 3;
                row->records[r] = check_vector_lower_record(move, t, t, f1);
                row->records[r++].decorations = 1;
                row->records[r] = check_vector_lower_record(compare, 1, f1, f1);
                row->records[r++].immediate = 7;
            }
            else
            {
                row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
                row->records[r++].immediate = 7;
            }
            row->records[r] = check_vector_lower_record(move, fr, fr, t);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
    }
    return reason;
}

// The row of __builtin_elementwise_ldexp(x, e) of vectors: e's elements to x's floating type by their bits' signed
// values, as llvm.ldexp takes them (VPMOVSXBD or VPMOVSXWD first for 8- and 16-bit ones, then VCVTDQ2PS or VCVTDQ2PD,
// or VCVTQQ2PS or VCVTQQ2PD with AVX-512DQ), then x times 2 to the floor of each (VSCALEFPS or VSCALEFPD), as clang -O0's
// code computes it. Its operands are x and e, of `vector`'s and `exponent`'s types.
static const char* check_vector_ldexp_row(const TypeTable* types, const X86Target* target, TypeId vector, TypeId exponent, Tier0Operation* row)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, vector);
    const CheckVectorShape ex = check_vector_builtin_shape(types, exponent);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)in.bytes;
    row->operand_count = 2;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)in.bytes;
    row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[1] = (uint8_t)ex.bytes;
    if (!in.floating || in.bfloat || in.element == 2)
    {
        reason = "elementwise vector builtins of half-precision or bf16 elements";
    }
    else if (!ex.integer || ex.element == 0 || ex.lanes != in.lanes)
    {
        reason = "vector ldexp of exponents of this type";
    }
    else
    {
        const bool d = in.element == 8;
        const uint32_t width = check_vector_builtin_width(in.bytes);
        const uint32_t dwords = check_vector_builtin_width(4 * in.lanes);
        uint32_t r = 0;
        if (ex.element <= 2)
        {
            static const CheckVectorForms extend[2] = { BUSTER_CHECK_VECTOR_FORMS(VPMOVSXBD_X_XM32_EVEX, VPMOVSXBD_Y_XM64_EVEX, VPMOVSXBD_Z_XM128),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPMOVSXWD_X_XM64_EVEX, VPMOVSXWD_Y_XM128_EVEX, VPMOVSXWD_Z_YM256) };
            row->records[r++] = check_vector_lower_record(extend[ex.element == 2].forms[dwords], v1, 0, v1);
        }
        if (ex.element <= 4)
        {
            static const CheckVectorForms convert[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTDQ2PS),
                                                         BUSTER_CHECK_VECTOR_FORMS(VCVTDQ2PD_X_XM64_EVEX, VCVTDQ2PD_Y_XM128_EVEX, VCVTDQ2PD_Z_YM256) };
            row->records[r++] = check_vector_lower_record(convert[d].forms[width], v1, 0, v1);
        }
        else
        {
            static const CheckVectorForms convert[2] = { BUSTER_CHECK_VECTOR_FORMS(VCVTQQ2PS_X_XM128, VCVTQQ2PS_X_YM256, VCVTQQ2PS_Y_ZM512),
                                                         BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTQQ2PD) };
            row->records[r++] = check_vector_lower_record(convert[d].forms[d ? width : check_vector_builtin_width(ex.bytes)], v1, 0, v1);
            reason = (target->features & BUSTER_X86_FEATURE(AVX512DQ)) == 0 ? "vector ldexp of 64-bit exponents without AVX-512DQ" : NULL;
        }
        static const CheckVectorForms scale[2] = { BUSTER_CHECK_VECTOR_PACKED(VSCALEFPS), BUSTER_CHECK_VECTOR_PACKED(VSCALEFPD) };
        row->records[r++] = check_vector_lower_record(scale[d].forms[width], v0, v0, v1);
        row->record_count = (uint8_t)r;
    }
    return reason;
}

// The most rows a floating reduction takes: a maximum's or minimum's halvings of 16 floats, a row each.
#define BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS 4u

// The rows of __builtin_reduce_maximum, _minimum or _assoc_fadd of a vector of floats or doubles into its element, as
// clang -O0's code reduces it: halving its bytes (the high 32 of 64 by VEXTRACTF64X4, the high 16 of 32 by
// VEXTRACTF32X4, then the high 8 of 16 by VPERMILPD 1 and of floats the high 4 of 8 by VMOVSHDUP, into v2), and combining
// the low part with the high one, the low one first: by llvm.maximum's or minimum's sequence (check_vector_floating_row's,
// its mask each row's second operand, a constant) or VADDP. A maximum's or minimum's row takes one halving, a sum's as
// many as its records hold. The last row's result is the element (FLOATING_RESULT); a sum's adds the tree's to the start
// value (VADDSS or VADDSD, the start's first, as clang adds them), its last operand (FLOATING), which the lowering gives
// -0.0 without one. `count` gets the rows' number.
static const char* check_vector_reduce_floating_rows(const TypeTable* types, CBuiltinId builtin, TypeId vector,
                                                     Tier0Operation rows[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS], uint32_t* count)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const char* reason = NULL;
    memset(rows, 0, BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS * sizeof(*rows));
    *count = 0;
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "vector reductions of half-precision or bf16 elements";
    }
    else
    {
        const bool d = shape.element == 8;
        const bool sum = builtin == CBUILTIN_REDUCE_ASSOC_FADD;
        const bool maximum = builtin == CBUILTIN_REDUCE_MAXIMUM;
        const uint8_t class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        static const CheckVectorForms maxima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMAXPS), BUSTER_CHECK_VECTOR_EVEX(VMAXPD) };
        static const CheckVectorForms minima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMINPS), BUSTER_CHECK_VECTOR_EVEX(VMINPD) };
        static const CheckVectorForms compares[2] = { BUSTER_CHECK_VECTOR_FORMS(VCMPPS_K_X_XM128_IMM8, VCMPPS_K_Y_YM256_IMM8, VCMPPS_K_Z_ZM512_IMM8),
                                                      BUSTER_CHECK_VECTOR_FORMS(VCMPPD_K_X_XM128_IMM8, VCMPPD_K_Y_YM256_IMM8, VCMPPD_K_Z_ZM512_IMM8) };
        static const CheckVectorForms moves[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPS), BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPD) };
        static const CheckVectorForms adds[2] = { BUSTER_CHECK_VECTOR_EVEX(VADDPS), BUSTER_CHECK_VECTOR_EVEX(VADDPD) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        uint32_t span = shape.bytes;
        uint32_t r = 0;
        uint32_t row = 0;
        // Each row's operands: the vector left, then a maximum's or minimum's mask.
        for (uint32_t i = 0; i < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS; i += 1)
        {
            rows[i].operand_count = sum ? 1 : 2;
            rows[i].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            rows[i].operand_classes[1] = sum ? 0 : BUSTER_TYPED_CLASS_VECTOR;
        }
        rows[0].operand_bytes[0] = (uint8_t)shape.bytes;
        rows[0].operand_bytes[1] = sum ? 0 : (uint8_t)shape.bytes;
        while (span > shape.element && row < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
        {
            const uint32_t half = span / 2;
            const uint32_t width = check_vector_builtin_width(half);
            X86Instruction halving[5];
            uint32_t h = 0;
            if (half >= 16)
            {
                halving[h] = check_vector_lower_record(half == 32 ? X86_FORM_VEXTRACTF64X4_YM256_Z_IMM8 : X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8, v0, 0, v2);
                halving[h++].immediate = 1;
            }
            else if (half == 8)
            {
                halving[h] = check_vector_lower_record(X86_FORM_VPERMILPD_X_XM128_IMM8_EVEX, v2, 0, v0);
                halving[h++].immediate = 1;
            }
            else
            {
                halving[h++] = check_vector_lower_record(X86_FORM_VMOVSHDUP_X_XM128_EVEX, v2, 0, v0);
            }
            if (sum)
            {
                halving[h++] = check_vector_lower_record(adds[d].forms[width], v0, v0, v2);
            }
            else
            {
                halving[h++] = check_vector_lower_record((maximum ? maxima : minima)[d].forms[width], v3, v0, v2);
                halving[h] = check_vector_lower_record(ternary.forms[width], v3, v0, v1);
                halving[h++].immediate = maximum ? 224 : 248;
                halving[h] = check_vector_lower_record(compares[d].forms[width], 1, v0, v0);
                halving[h++].immediate = 7;
                halving[h] = check_vector_lower_record(moves[d].forms[width], v0, 0, v3);
                halving[h++].decorations = 1;
            }
            // A sum's last row keeps a record for the start's addition; a maximum's or minimum's, for the move out.
            const bool last = half == shape.element;
            if ((r + h + (last ? 1u : 0u) > BUSTER_TIER0_OPERATION_RECORDS || (!sum && r != 0)) && row + 1 < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
            {
                // This row ends before the halving, its result the vector of the bytes left, the next row's operand.
                rows[row].record_count = (uint8_t)r;
                rows[row].result_class = BUSTER_TYPED_CLASS_VECTOR;
                rows[row].result_bytes = (uint8_t)span;
                row += 1;
                rows[row].operand_bytes[0] = (uint8_t)span;
                rows[row].operand_bytes[1] = sum ? 0 : (uint8_t)span;
                r = 0;
            }
            if (r + h + (last ? 1u : 0u) <= BUSTER_TIER0_OPERATION_RECORDS)
            {
                memcpy(rows[row].records + r, halving, h * sizeof(*halving));
            }
            else
            {
                // A vector has 16 elements at most: a maximum's four halvings take the four rows exactly.
                reason = "vector reductions of more elements than their rows hold";
            }
            r += h;
            span = half;
        }
        if (reason == NULL)
        {
            Tier0Operation* current = rows + row;
            if (sum)
            {
                // The start's addition: start + tree, the start the row's last operand.
                const uint32_t start = current->operand_count;
                current->operand_classes[start] = class;
                current->operand_count = (uint8_t)(start + 1);
                current->records[r++] = check_vector_lower_record(d ? X86_FORM_VADDSD_X_X_XM64_EVEX : X86_FORM_VADDSS_X_X_XM32_EVEX,
                                                                  BUSTER_TIER0_ROLE_FLOATING_RESULT, BUSTER_TIER0_ROLE_FLOATING(start), v0);
            }
            else
            {
                current->records[r++] = check_vector_lower_record(X86_FORM_VMOVAPS_X_XM128_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, v0);
            }
            current->record_count = (uint8_t)r;
            current->result_class = class;
            *count = row + 1;
        }
    }
    return reason;
}

// The row of __builtin_nontemporal_store(value, p) or __builtin_nontemporal_load(p) of a vector of `vector`'s type at
// an address aligned to its bytes: VMOVNTPS, VMOVNTPD or VMOVNTDQ of the value by its elements, or VMOVNTDQA's load of
// any, as clang's code's are; of a vector of 4 or 8 bytes, its bits through a scratch register stored by MOVNTI
// (clang's for MMX's __m64, which takes SSE4A's MOVNTSD for floats where the target has it), and an ordinary load
// (clang's). A store's operands are the value and the address, a load's the address.
static void check_vector_nontemporal_row(const TypeTable* types, TypeId vector, bool store, Tier0Operation* row)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const bool narrow = shape.bytes < 16;
    const bool wide = shape.bytes == 8;
    memset(row, 0, sizeof(*row));
    row->record_count = 1;
    if (store && narrow)
    {
        row->records[0] = check_vector_lower_record(wide ? X86_FORM_VMOVQ_RM64_X_EVEX : X86_FORM_VMOVD_RM32_X_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                                    BUSTER_TIER0_ROLE_SCRATCH(0));
        row->records[1] = check_x86_lower_memory(wide ? X86_FORM_MOVNTI_M64_R64 : X86_FORM_MOVNTI_M32_R32, BUSTER_TIER0_ROLE_SCRATCH(0), BUSTER_TIER0_ROLE_INTEGER(1));
        row->record_count = 2;
        row->operand_count = 2;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)shape.bytes;
        row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VOID;
    }
    else if (narrow)
    {
        row->records[0] = check_x86_lower_memory(wide ? X86_FORM_VMOVQ_X_XM64_EVEX : X86_FORM_VMOVD_X_RM32_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(0));
        row->operand_count = 1;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
    else if (store)
    {
        static const CheckVectorForms stores[3] = {
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTPS_M128_X_EVEX, VMOVNTPS_M256_Y_EVEX, VMOVNTPS_M512_Z),
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTPD_M128_X_EVEX, VMOVNTPD_M256_Y_EVEX, VMOVNTPD_M512_Z),
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTDQ_M128_X_EVEX, VMOVNTDQ_M256_Y_EVEX, VMOVNTDQ_M512_Z),
        };
        const uint32_t kind = shape.floating && shape.element == 4 ? 0 : shape.floating && shape.element == 8 ? 1 : 2;
        row->records[0] = check_x86_lower_memory(stores[kind].forms[width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(1));
        row->operand_count = 2;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)shape.bytes;
        row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VOID;
    }
    else
    {
        static const CheckVectorForms loads = BUSTER_CHECK_VECTOR_FORMS(VMOVNTDQA_X_M128_EVEX, VMOVNTDQA_Y_M256_EVEX, VMOVNTDQA_Z_M512);
        row->records[0] = check_x86_lower_memory(loads.forms[width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(0));
        row->operand_count = 1;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
}

// The most rows a reduction takes: a row's 8 records hold three halvings and the element's move.
#define BUSTER_CHECK_VECTOR_REDUCE_ROWS 2u

// The rows of an integer __builtin_reduce_* function of a vector into its element: halves combined by the operation
// down to one element (the high 32 bytes of 64 by VEXTRACTI64X4, the high 16 of 32 by VEXTRACTI32X4, then within 16
// bytes the high 8 by VPSHUFD 0x4e, the high 4 by VPSHUFD 0xb1, the high 2 by VPSRLD 16 and the high byte by VPSRLW 8,
// each beside the low part in v1), then that element in the general register (VMOVQ, VMOVD, VPEXTRW or VPEXTRB). A
// halving is two records. Byte product reductions first widen to words (pairing widened halves for 64-byte inputs).
// Where a row's eight records aren't enough, split between complete halvings and carry the low live vector bytes
// into the second row, at least 16. `count` gets the rows' number.
static const char* check_vector_reduce_rows(const TypeTable* types, const X86Target* target, CBuiltinId builtin, TypeId vector,
                                            Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS], uint32_t* count)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
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

// Whether a reduction's vector of `vector`'s type has lanes past its elements (an ext_vector_type of 3 floats or of 6
// shorts, padded to a power of two), whose bytes may hold anything; then `row` gives the vector its operation's identity
// in them, as LLVM's widening of the vector does before clang's code reduces it, so that the reduction's halvings
// (check_vector_reduce_rows, check_vector_reduce_floating_rows) take the elements alone: (v & keep) | fill, VPTERNLOGD
// 0xEA of the vector and its constant operands `keep` (all ones in the elements' bytes) and `fill` (the identity past
// them): 0 for add, or, xor and an unsigned max; all ones for and and an unsigned min; 1 for mul; the largest value for
// a signed min and the smallest for a signed max; -inf for maximum, +inf for minimum and -0.0 for assoc_fadd.
static bool check_vector_reduce_fill_row(const TypeTable* types, CBuiltinId builtin, TypeId vector, Tier0Operation* row, uint64_t keep[8], uint64_t fill[8])
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t used = shape.lanes * shape.element;
    const bool padded = shape.element != 0 && used < shape.bytes && shape.bytes <= 64;
    memset(row, 0, sizeof(*row));
    memset(keep, 0, 8 * sizeof(*keep));
    memset(fill, 0, 8 * sizeof(*fill));
    if (padded)
    {
        const bool d = shape.element == 8;
        const uint64_t ones = d ? UINT64_MAX : (UINT64_C(1) << (8 * shape.element)) - 1;
        uint64_t identity = 0;
        if (builtin == CBUILTIN_REDUCE_AND || (builtin == CBUILTIN_REDUCE_MIN && !shape.is_signed))
        {
            identity = ones;
        }
        else if (builtin == CBUILTIN_REDUCE_MUL)
        {
            identity = 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MIN)
        {
            identity = ones >> 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MAX && shape.is_signed)
        {
            identity = (ones >> 1) + 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MAXIMUM || builtin == CBUILTIN_REDUCE_MINIMUM)
        {
            // The infinity of the element's type, negative for maximum.
            identity = (d ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000)) | (builtin == CBUILTIN_REDUCE_MAXIMUM ? check_vector_floating_sign(d) : 0);
        }
        else if (builtin == CBUILTIN_REDUCE_ASSOC_FADD)
        {
            identity = check_vector_floating_sign(d);
        }
        uint8_t* kept = (uint8_t*)keep;
        uint8_t* filled = (uint8_t*)fill;
        for (uint32_t b = 0; b < shape.bytes; b += 1)
        {
            kept[b] = b < used ? 0xff : 0;
            filled[b] = b < used ? 0 : (uint8_t)(identity >> (8 * (b % shape.element)));
        }
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        row->records[0] = check_vector_lower_record(ternary.forms[check_vector_builtin_width(shape.bytes)], BUSTER_TIER0_ROLE_VECTOR(0),
                                                    BUSTER_TIER0_ROLE_VECTOR(1), BUSTER_TIER0_ROLE_VECTOR(2));
        row->records[0].immediate = 0xea;
        row->record_count = 1;
        row->operand_count = 3;
        for (uint32_t j = 0; j < 3; j += 1)
        {
            row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[j] = (uint8_t)shape.bytes;
        }
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
    return padded;
}
