// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// SSE2/AVX vector operators. Rows use the target's features, not CPUID. Legacy instructions tie destination to
// their first source; explicit copies preserve that constraint. Unsupported widths/operations fail before emission.
// The AVX-512 row builder stays independent so its existing records and floating-point behavior are unchanged.

typedef struct CheckVectorBaseline CheckVectorBaseline;
struct CheckVectorBaseline
{
    Tier0Operation* row;
    uint32_t bytes;
    bool avx;
    bool valid;
};

#define BUSTER_CHECK_BASE_PACKED(context, name) ((context)->bytes > 16 ? X86_FORM_V##name##_Y_Y_YM256 : \
                                                (context)->avx ? X86_FORM_V##name##_X_X_XM128 : X86_FORM_##name##_X_XM128)
// A unary form by its 16-byte operands (`x`) and 32-byte ones (`y`). This and BUSTER_CHECK_BASE_PACKED stay defined past
// this file, for c_check_program_vector_builtins.c's conversion rows, which c_check_program_lower.c includes after it and
// which undefines them at its end.
#define BUSTER_CHECK_BASE_UNARY(context, name, x, y) ((context)->bytes > 16 ? X86_FORM_V##name##_##y : \
                                                     (context)->avx ? X86_FORM_V##name##_##x : X86_FORM_##name##_##x)

static void check_vector_baseline_put(CheckVectorBaseline* context, X86Instruction record)
{
    Tier0Operation* row = context->row;
    if (row->record_count < BUSTER_TIER0_OPERATION_RECORDS && record.form != 0)
    {
        row->records[row->record_count] = record;
        row->record_count += 1;
    }
    else
    {
        context->valid = false;
    }
}

static void check_vector_baseline_copy(CheckVectorBaseline* context, uint8_t to, uint8_t from)
{
    if (to != from)
    {
        const uint32_t form = context->bytes > 16 ? X86_FORM_VMOVUPS_Y_YM256 : context->avx ? X86_FORM_VMOVUPS_X_XM128 : X86_FORM_MOVUPS_X_XM;
        check_vector_baseline_put(context, check_vector_lower_record(form, to, 0, from));
    }
}

static void check_vector_baseline_binary(CheckVectorBaseline* context, uint32_t form, uint8_t to, uint8_t first, uint8_t second)
{
    // Callers never copy over a distinct second source. That would need another temporary, not a destructive copy.
    const bool vex = x86_form_description(form)->family == BUSTER_X86_FAMILY_VEX;
    if (!vex && to != first && to == second)
    {
        context->valid = false;
    }
    else
    {
        if (!vex)
        {
            check_vector_baseline_copy(context, to, first);
        }
        check_vector_baseline_put(context, check_vector_lower_record(form, to, vex ? first : 0, second));
    }
}

static void check_vector_baseline_shuffle(CheckVectorBaseline* context, uint8_t to, uint8_t from, uint8_t immediate)
{
    const uint32_t form = context->bytes > 16 ? X86_FORM_VPSHUFD_Y_YM256_IMM8 :
                          context->avx ? X86_FORM_VPSHUFD_X_XM128_IMM8 : X86_FORM_PSHUFD_X_XM128_IMM8;
    X86Instruction record = check_vector_lower_record(form, to, 0, from);
    record.immediate = immediate;
    check_vector_baseline_put(context, record);
}

// An immediate shift of `value`'s elements of `element` bytes by `bits`: left, logical right or arithmetic right (`kind`
// 0, 1 or 2), at the context's width. SSE2 has no arithmetic shift of qwords.
static void check_vector_baseline_shift_kind(CheckVectorBaseline* context, uint32_t kind, uint8_t value, uint32_t element, uint8_t bits)
{
    // By kind, element (words, dwords, qwords) and form (legacy, VEX of 16 bytes, VEX of 32).
    static const uint16_t forms[3][3][3] = {
        {
            { X86_FORM_PSLLW_X_IMM8, X86_FORM_VPSLLW_X_X_IMM8, X86_FORM_VPSLLW_Y_Y_IMM8 },
            { X86_FORM_PSLLD_X_IMM8, X86_FORM_VPSLLD_X_X_IMM8, X86_FORM_VPSLLD_Y_Y_IMM8 },
            { X86_FORM_PSLLQ_X_IMM8, X86_FORM_VPSLLQ_X_X_IMM8, X86_FORM_VPSLLQ_Y_Y_IMM8 },
        },
        {
            { X86_FORM_PSRLW_X_IMM8, X86_FORM_VPSRLW_X_X_IMM8, X86_FORM_VPSRLW_Y_Y_IMM8 },
            { X86_FORM_PSRLD_X_IMM8, X86_FORM_VPSRLD_X_X_IMM8, X86_FORM_VPSRLD_Y_Y_IMM8 },
            { X86_FORM_PSRLQ_X_IMM8, X86_FORM_VPSRLQ_X_X_IMM8, X86_FORM_VPSRLQ_Y_Y_IMM8 },
        },
        {
            { X86_FORM_PSRAW_X_IMM8, X86_FORM_VPSRAW_X_X_IMM8, X86_FORM_VPSRAW_Y_Y_IMM8 },
            { X86_FORM_PSRAD_X_IMM8, X86_FORM_VPSRAD_X_X_IMM8, X86_FORM_VPSRAD_Y_Y_IMM8 },
            { 0, 0, 0 },
        },
    };
    const uint32_t size = element == 2 ? 0 : element == 4 ? 1 : 2;
    const uint32_t width = context->bytes > 16 ? 2 : context->avx ? 1 : 0;
    X86Instruction record = check_vector_lower_record(forms[kind][size][width], 0, context->avx ? value : 0, value);
    record.immediate = bits;
    check_vector_baseline_put(context, record);
}

static void check_vector_baseline_shift(CheckVectorBaseline* context, uint8_t value, uint32_t element, uint8_t bits)
{
    check_vector_baseline_shift_kind(context, 0, value, element, bits);
}

// VECTOR(0)'s high 16 bytes into VECTOR(1) (VEXTRACTF128), the rest of the context's records on 16 bytes.
static void check_vector_baseline_high_half(CheckVectorBaseline* context)
{
    X86Instruction record = check_vector_lower_record(X86_FORM_VEXTRACTF128_XM128_Y_IMM8, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(1));
    record.immediate = 1;
    check_vector_baseline_put(context, record);
    context->bytes = 16;
}

// VECTOR(0)'s integers of `from` bytes to integers of `to` bytes in place, where the target hasn't AVX-512's VPMOV forms
// (or, for an extension, SSE4.1's PMOVSX and PMOVZX), as LLVM's legalization lowers them; VECTOR(1) is its scratch and the
// context's width the source's. An extension unpacks the low elements: with zeros by the source's signedness, or with
// themselves, which leaves the element's bits at the top of a word or dword for an arithmetic shift right to extend, a
// qword's high dword the low one's sign (PSRAD by 31). A truncation takes a qword's low dword (PSHUFD, or with a source
// of 32 bytes SHUFPS of its halves), then makes each element's kept bits its whole value, sign-extended in a dword or
// zero-extended in a word, so that the saturating packs keep them exactly (PACKSSDW, PACKSSWB, PACKUSWB), a source of 32
// bytes packing its two halves. The elements past the result's in the register are unspecified.
static void check_vector_baseline_resize(CheckVectorBaseline* context, uint32_t from, uint32_t to, bool is_signed)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    uint32_t size = from;
    if (from < to && !is_signed)
    {
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, XORPS), v1, v1, v1);
        while (size < to)
        {
            const uint32_t form = size == 1 ? BUSTER_CHECK_BASE_PACKED(context, PUNPCKLBW) :
                                  size == 2 ? BUSTER_CHECK_BASE_PACKED(context, PUNPCKLWD) : BUSTER_CHECK_BASE_PACKED(context, PUNPCKLDQ);
            check_vector_baseline_binary(context, form, v0, v0, v1);
            size *= 2;
        }
    }
    else if (from < to)
    {
        const uint32_t top = to < 4 ? to : 4;
        while (size < top)
        {
            const uint32_t form = size == 1 ? BUSTER_CHECK_BASE_PACKED(context, PUNPCKLBW) : BUSTER_CHECK_BASE_PACKED(context, PUNPCKLWD);
            check_vector_baseline_binary(context, form, v0, v0, v0);
            size *= 2;
        }
        if (top > from)
        {
            check_vector_baseline_shift_kind(context, 2, v0, top, (uint8_t)(8 * (top - from)));
        }
        if (to == 8)
        {
            check_vector_baseline_copy(context, v1, v0);
            check_vector_baseline_shift_kind(context, 2, v1, 4, 31);
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PUNPCKLDQ), v0, v0, v1);
        }
    }
    else if (from > to)
    {
        if (from == 8 && context->bytes > 16)
        {
            check_vector_baseline_high_half(context);
            X86Instruction record = check_vector_lower_record(X86_FORM_VSHUFPS_X_X_XM128_IMM8, v0, v0, v1);
            record.immediate = 0x88;
            check_vector_baseline_put(context, record);
            size = 4;
        }
        else if (from == 8)
        {
            check_vector_baseline_shuffle(context, v0, v0, 0x88);
            size = 4;
        }
        if (size > to)
        {
            const uint8_t bits = (uint8_t)(8 * (size - to));
            check_vector_baseline_shift_kind(context, 0, v0, size, bits);
            check_vector_baseline_shift_kind(context, size == 2 ? 1 : 2, v0, size, bits);
            const bool halves = context->bytes > 16;
            if (halves)
            {
                check_vector_baseline_high_half(context);
            }
            const uint32_t pack = size == 2 ? BUSTER_CHECK_BASE_PACKED(context, PACKUSWB) : BUSTER_CHECK_BASE_PACKED(context, PACKSSDW);
            check_vector_baseline_binary(context, pack, v0, v0, halves ? v1 : v0);
            if (size == 4 && to == 1)
            {
                check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PACKSSWB), v0, v0, v0);
            }
        }
    }
}

static void check_vector_baseline_ones(CheckVectorBaseline* context, uint8_t value)
{
    check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PCMPEQD), value, value, value);
}

// First splat in 128 bits. AVX can replicate that half with VINSERTF128 even without AVX2's integer broadcasts.
static void check_vector_baseline_splat(CheckVectorBaseline* context, uint8_t to, uint8_t from, uint32_t element, bool floating)
{
    const uint32_t bytes = context->bytes;
    context->bytes = 16;
    if (floating)
    {
        check_vector_baseline_shuffle(context, to, from, element == 8 ? 0x44 : 0);
    }
    else
    {
        const uint32_t move = element == 8 ? (context->avx ? X86_FORM_VMOVQ_X_RM64 : X86_FORM_MOVQ_X_RM64) :
                                            (context->avx ? X86_FORM_VMOVD_X_RM32 : X86_FORM_MOVD_X_RM32);
        check_vector_baseline_put(context, check_vector_lower_record(move, to, 0, from));
        if (element == 1)
        {
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PUNPCKLBW), to, to, to);
        }
        if (element <= 2)
        {
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PUNPCKLWD), to, to, to);
        }
        check_vector_baseline_shuffle(context, to, to, element == 8 ? 0x44 : 0);
    }
    context->bytes = bytes;
    if (bytes > 16)
    {
        X86Instruction record = check_vector_lower_record(X86_FORM_VINSERTF128_Y_Y_XM128_IMM8, to, to, to);
        record.immediate = 1;
        check_vector_baseline_put(context, record);
    }
}

static uint32_t check_vector_baseline_binary_form(const CheckVectorBaseline* context, CheckVectorElement element, uint32_t op)
{
    const uint32_t b = element.bytes;
    uint32_t form = 0;
    if (element.floating && b >= 4)
    {
        form = op == CHECK_OP_ADD ? (b == 4 ? BUSTER_CHECK_BASE_PACKED(context, ADDPS) : BUSTER_CHECK_BASE_PACKED(context, ADDPD)) :
               op == CHECK_OP_SUBTRACT ? (b == 4 ? BUSTER_CHECK_BASE_PACKED(context, SUBPS) : BUSTER_CHECK_BASE_PACKED(context, SUBPD)) :
               op == CHECK_OP_MULTIPLY ? (b == 4 ? BUSTER_CHECK_BASE_PACKED(context, MULPS) : BUSTER_CHECK_BASE_PACKED(context, MULPD)) :
               op == CHECK_OP_DIVIDE ? (b == 4 ? BUSTER_CHECK_BASE_PACKED(context, DIVPS) : BUSTER_CHECK_BASE_PACKED(context, DIVPD)) : 0;
    }
    else if (!element.floating && (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT))
    {
        const bool add = op == CHECK_OP_ADD;
        form = b == 1 ? (add ? BUSTER_CHECK_BASE_PACKED(context, PADDB) : BUSTER_CHECK_BASE_PACKED(context, PSUBB)) :
               b == 2 ? (add ? BUSTER_CHECK_BASE_PACKED(context, PADDW) : BUSTER_CHECK_BASE_PACKED(context, PSUBW)) :
               b == 4 ? (add ? BUSTER_CHECK_BASE_PACKED(context, PADDD) : BUSTER_CHECK_BASE_PACKED(context, PSUBD)) :
                        (add ? BUSTER_CHECK_BASE_PACKED(context, PADDQ) : BUSTER_CHECK_BASE_PACKED(context, PSUBQ));
    }
    else if (!element.floating && op == CHECK_OP_MULTIPLY)
    {
        form = b == 2 ? BUSTER_CHECK_BASE_PACKED(context, PMULLW) : b == 4 ? BUSTER_CHECK_BASE_PACKED(context, PMULLD) : 0;
    }
    else if (op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_OR || op == CHECK_OP_BIT_XOR)
    {
        // Floating bitwise instructions preserve every bit and need only AVX at 256 bits, not AVX2.
        form = op == CHECK_OP_BIT_AND ? BUSTER_CHECK_BASE_PACKED(context, ANDPS) :
               op == CHECK_OP_BIT_OR ? BUSTER_CHECK_BASE_PACKED(context, ORPS) : BUSTER_CHECK_BASE_PACKED(context, XORPS);
    }
    else if (context->avx && !element.floating && (op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT))
    {
        const bool left = op == CHECK_OP_SHIFT_LEFT;
        const bool wide = context->bytes > 16;
        form = b == 4 ? (left ? (wide ? X86_FORM_VPSLLVD_Y_Y_YM256 : X86_FORM_VPSLLVD_X_X_XM128) :
                        element.is_signed ? (wide ? X86_FORM_VPSRAVD_Y_Y_YM256 : X86_FORM_VPSRAVD_X_X_XM128) :
                                            (wide ? X86_FORM_VPSRLVD_Y_Y_YM256 : X86_FORM_VPSRLVD_X_X_XM128)) :
               b == 8 && (left || !element.is_signed) ? (left ? (wide ? X86_FORM_VPSLLVQ_Y_Y_YM256 : X86_FORM_VPSLLVQ_X_X_XM128) :
                                                                       (wide ? X86_FORM_VPSRLVQ_Y_Y_YM256 : X86_FORM_VPSRLVQ_X_X_XM128)) : 0;
    }
    return form;
}

static void check_vector_baseline_compare(CheckVectorBaseline* context, CheckVectorElement element, uint32_t op)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint32_t b = element.bytes;
    if (element.floating)
    {
        const bool swap = op == CHECK_OP_GREATER || op == CHECK_OP_GREATER_EQUAL;
        const uint8_t first = swap ? v1 : v0;
        const uint32_t form = context->bytes > 16 ? (b == 4 ? X86_FORM_VCMPPS_Y_Y_YM256_IMM8 : X86_FORM_VCMPPD_Y_Y_YM256_IMM8) :
                              context->avx ? (b == 4 ? X86_FORM_VCMPPS_X_X_XM128_IMM8 : X86_FORM_VCMPPD_X_X_XM128_IMM8) :
                                             (b == 4 ? X86_FORM_CMPPS_X_XM128_IMM8 : X86_FORM_CMPPD_X_XM128_IMM8);
        const uint8_t predicate = op == CHECK_OP_EQUAL ? 0 : op == CHECK_OP_NOT_EQUAL ? 4 :
                                  op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER_EQUAL ? 2 : 1;
        check_vector_baseline_binary(context, form, first, first, swap ? v0 : v1);
        context->row->records[context->row->record_count - 1].immediate = predicate;
        check_vector_baseline_copy(context, v0, first);
    }
    else
    {
        const bool equality = op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL;
        if (!element.is_signed && !equality)
        {
            // Bias both operands' sign bits, reducing unsigned ordering to signed ordering. For bytes, pack -128
            // from each 16-bit lane to get 0x80 in every byte, rather than shifting across byte boundaries.
            check_vector_baseline_ones(context, v2);
            check_vector_baseline_shift(context, v2, b == 1 ? 2 : b, (uint8_t)(8 * b - 1));
            if (b == 1)
            {
                check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PACKSSWB), v2, v2, v2);
            }
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, XORPS), v0, v0, v2);
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, XORPS), v1, v1, v2);
        }
        const bool swap = op == CHECK_OP_LESS || op == CHECK_OP_GREATER_EQUAL;
        const bool invert = op == CHECK_OP_NOT_EQUAL || op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER_EQUAL;
        const uint8_t first = swap ? v1 : v0;
        const uint32_t form = b == 1 ? (equality ? BUSTER_CHECK_BASE_PACKED(context, PCMPEQB) : BUSTER_CHECK_BASE_PACKED(context, PCMPGTB)) :
                              b == 2 ? (equality ? BUSTER_CHECK_BASE_PACKED(context, PCMPEQW) : BUSTER_CHECK_BASE_PACKED(context, PCMPGTW)) :
                              b == 4 ? (equality ? BUSTER_CHECK_BASE_PACKED(context, PCMPEQD) : BUSTER_CHECK_BASE_PACKED(context, PCMPGTD)) :
                                       (equality ? BUSTER_CHECK_BASE_PACKED(context, PCMPEQQ) : BUSTER_CHECK_BASE_PACKED(context, PCMPGTQ));
        check_vector_baseline_binary(context, form, first, first, swap ? v0 : v1);
        if (invert)
        {
            const uint8_t ones = swap ? v0 : v2;
            check_vector_baseline_ones(context, ones);
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, XORPS), v0, v0, swap ? v1 : v2);
        }
        else
        {
            check_vector_baseline_copy(context, v0, first);
        }
    }
}

// A shift of VECTOR(0)'s integers of `element` bytes by the count in INTEGER(1), a U32 (#1834), as clang's code shifts by a
// splat count without AVX-512: the count into VECTOR(1) (MOVD, zeros above it), then PSLL, PSRL or PSRA of words, dwords
// and qwords by it. SSE has no arithmetic shift of qwords: it is the logical one, xor the sign bit shifted alike, less
// that (PCMPEQD and PSLLQ by 63 make the sign bit). Nor has it shifts of bytes: left and logical right ones shift words,
// their bytes masked by 0xFF shifted alike (PCMPEQD, PSRLW by 8 and by the count, PACKUSWB) before a left shift or after
// a right one; an arithmetic right one unpacks each half's bytes into words' high bytes (PUNPCKHBW and PUNPCKLBW), shifts
// them by the count and by 8 (PSRAW) and packs them back (PACKSSWB), a word's low byte any (VECTOR(2)'s).
static void check_vector_baseline_shift_by(CheckVectorBaseline* context, bool left, CheckVectorElement element)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const bool wide = context->bytes > 16;
    // By kind (left, logical right, arithmetic right) and element (words, dwords, qwords): legacy, VEX of 16 and of 32 bytes.
    static const uint16_t forms[3][3][3] = {
        {
            { X86_FORM_PSLLW_X_XM128, X86_FORM_VPSLLW_X_X_XM128, X86_FORM_VPSLLW_Y_Y_XM128 },
            { X86_FORM_PSLLD_X_XM128, X86_FORM_VPSLLD_X_X_XM128, X86_FORM_VPSLLD_Y_Y_XM128 },
            { X86_FORM_PSLLQ_X_XM128, X86_FORM_VPSLLQ_X_X_XM128, X86_FORM_VPSLLQ_Y_Y_XM128 },
        },
        {
            { X86_FORM_PSRLW_X_XM128, X86_FORM_VPSRLW_X_X_XM128, X86_FORM_VPSRLW_Y_Y_XM128 },
            { X86_FORM_PSRLD_X_XM128, X86_FORM_VPSRLD_X_X_XM128, X86_FORM_VPSRLD_Y_Y_XM128 },
            { X86_FORM_PSRLQ_X_XM128, X86_FORM_VPSRLQ_X_X_XM128, X86_FORM_VPSRLQ_Y_Y_XM128 },
        },
        {
            { X86_FORM_PSRAW_X_XM128, X86_FORM_VPSRAW_X_X_XM128, X86_FORM_VPSRAW_Y_Y_XM128 },
            { X86_FORM_PSRAD_X_XM128, X86_FORM_VPSRAD_X_X_XM128, X86_FORM_VPSRAD_Y_Y_XM128 },
            { 0, 0, 0 },
        },
    };
    const uint32_t width = wide ? 2 : context->avx ? 1 : 0;
    const uint32_t kind = left ? 0 : element.is_signed ? 2 : 1;
    const uint32_t move = context->avx ? X86_FORM_VMOVD_X_RM32 : X86_FORM_MOVD_X_RM32;
    check_vector_baseline_put(context, check_vector_lower_record(move, v1, 0, BUSTER_TIER0_ROLE_INTEGER(1)));
    if (element.bytes == 1 && kind == 2)
    {
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PUNPCKHBW), v2, v2, v0);
        check_vector_baseline_binary(context, forms[2][0][width], v2, v2, v1);
        check_vector_baseline_shift_kind(context, 2, v2, 2, 8);
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PUNPCKLBW), v0, v0, v0);
        check_vector_baseline_binary(context, forms[2][0][width], v0, v0, v1);
        check_vector_baseline_shift_kind(context, 2, v0, 2, 8);
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PACKSSWB), v0, v0, v2);
    }
    else if (element.bytes == 1)
    {
        check_vector_baseline_ones(context, v2);
        check_vector_baseline_shift_kind(context, 1, v2, 2, 8);
        check_vector_baseline_binary(context, forms[1][0][width], v2, v2, v1);
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PACKUSWB), v2, v2, v2);
        if (left)
        {
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PAND), v0, v0, v2);
        }
        check_vector_baseline_binary(context, forms[kind][0][width], v0, v0, v1);
        if (!left)
        {
            check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PAND), v0, v0, v2);
        }
    }
    else if (element.bytes == 8 && kind == 2)
    {
        check_vector_baseline_binary(context, forms[1][2][width], v0, v0, v1);
        check_vector_baseline_ones(context, v2);
        check_vector_baseline_shift_kind(context, 0, v2, 8, 63);
        check_vector_baseline_binary(context, forms[1][2][width], v2, v2, v1);
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PXOR), v0, v0, v2);
        check_vector_baseline_binary(context, BUSTER_CHECK_BASE_PACKED(context, PSUBQ), v0, v0, v2);
    }
    else
    {
        check_vector_baseline_binary(context, forms[kind][element.bytes == 2 ? 0 : element.bytes == 4 ? 1 : 2][width], v0, v0, v1);
    }
}

// An operator's row on `bytes` bytes of `element`s: a vector's own, or a piece of a wider one's that the target's
// registers hold (check_lower_vector_split, #1193), or of a transport vector's (check_lower_transport_rows, #137). Every
// row here is elementwise, a piece's elements its own.
static const char* check_vector_lower_baseline_element(const X86Target* target, uint32_t op, CheckVectorElement element, uint32_t bytes, uint8_t scalar_class,
                                                      Tier0Operation* row)
{
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const bool compare = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)bytes;
    row->operand_count = op == BUSTER_CHECK_VECTOR_SPLAT || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ? 1 : 2;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)bytes;
    }
    CheckVectorBaseline context = { row, bytes, avx, true };
    if (bytes > (avx ? 32u : 16u))
    {
        reason = "vector operations wider than the target's registers";
    }
    else if (element.bytes == 0 || (element.floating && element.bytes < 4))
    {
        reason = "vector operations on this element type without AVX-512";
    }
    else if (op == BUSTER_CHECK_VECTOR_SHIFT_LEFT_BY || op == BUSTER_CHECK_VECTOR_SHIFT_RIGHT_BY)
    {
        row->operand_classes[1] = scalar_class;
        row->operand_bytes[1] = 0;
        context.valid = !element.floating && scalar_class == BUSTER_TYPED_CLASS_U32;
        check_vector_baseline_shift_by(&context, op == BUSTER_CHECK_VECTOR_SHIFT_LEFT_BY, element);
    }
    else if (op == BUSTER_CHECK_VECTOR_SPLAT || (op == CHECK_OP_NEGATIVE && element.floating))
    {
        const bool splat = op == BUSTER_CHECK_VECTOR_SPLAT;
        const uint32_t operand = splat ? 0 : 1;
        row->operand_count = (uint8_t)(operand + 1);
        row->operand_classes[operand] = splat ? scalar_class : element.bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
        row->operand_bytes[operand] = 0;
        const bool floating = splat && element.floating;
        check_vector_baseline_splat(&context, splat ? v0 : v1, floating ? BUSTER_TIER0_ROLE_FLOATING(operand) : BUSTER_TIER0_ROLE_INTEGER(operand),
                                     element.bytes, floating);
        if (!splat)
        {
            check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, XORPS), v0, v0, v1);
        }
    }
    else if (compare)
    {
        check_vector_baseline_compare(&context, element, op);
    }
    else if (op == CHECK_OP_BIT_NOT)
    {
        check_vector_baseline_ones(&context, v1);
        check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, XORPS), v0, v0, v1);
    }
    else if (op == CHECK_OP_NEGATIVE)
    {
        check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, XORPS), v1, v1, v1);
        check_vector_baseline_binary(&context, check_vector_baseline_binary_form(&context, element, CHECK_OP_SUBTRACT), v1, v1, v0);
        check_vector_baseline_copy(&context, v0, v1);
    }
    else if (op == CHECK_OP_MULTIPLY && !element.floating && element.bytes == 4 && !avx && (target->features & BUSTER_X86_FEATURE(SSE41)) == 0)
    {
        // SSE2's PMULUDQ multiplies even lanes. The low halves of signed and unsigned products agree modulo 2^32.
        check_vector_baseline_shuffle(&context, v2, v0, 0xf5);
        check_vector_baseline_shuffle(&context, v3, v1, 0xf5);
        check_vector_baseline_binary(&context, X86_FORM_PMULUDQ_X_XM128, v0, v0, v1);
        check_vector_baseline_binary(&context, X86_FORM_PMULUDQ_X_XM128, v2, v2, v3);
        check_vector_baseline_shuffle(&context, v0, v0, 0x88);
        check_vector_baseline_shuffle(&context, v2, v2, 0x88);
        check_vector_baseline_binary(&context, X86_FORM_PUNPCKLDQ_X_XM128, v0, v0, v2);
    }
    else if (element.floating && op == CHECK_OP_DIVIDE && bytes < 16)
    {
        // Unused zero lanes would execute 0/0 and spuriously raise FE_INVALID; do not silently use a packed divide.
        reason = "division of narrow floating vectors without AVX-512 masking";
    }
    else
    {
        const uint32_t form = check_vector_baseline_binary_form(&context, element, op);
        if (form != 0)
        {
            check_vector_baseline_binary(&context, form, v0, v0, v1);
        }
        else
        {
            context.valid = false;
        }
    }
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    for (uint32_t r = 0; context.valid && r < row->record_count; r += 1)
    {
        const uint64_t needed = x86_target_requirement(row->records[r].form);
        context.valid = needed != 0 && (needed & ~available) == 0;
    }
    if (reason == NULL && !context.valid)
    {
        reason = op == CHECK_OP_MULTIPLY ? "vector multiplication unavailable at this target" :
                 op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == BUSTER_CHECK_VECTOR_SHIFT_LEFT_BY || op == BUSTER_CHECK_VECTOR_SHIFT_RIGHT_BY ?
                     "vector shifts unavailable at this target" :
                 op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER ? "vector division of integers" :
                 compare ? "vector comparison unavailable at this target" : "vector operator unavailable at this target";
    }
    return reason;
}

static const char* check_vector_lower_baseline_piece(const TypeTable* types, const X86Target* target, uint32_t op, TypeId vector, uint32_t bytes,
                                                    uint8_t scalar_class, Tier0Operation* row)
{
    return check_vector_lower_baseline_element(target, op, check_vector_lower_element(types, vector), bytes, scalar_class, row);
}

static const char* check_vector_lower_baseline_row(const TypeTable* types, const X86Target* target, uint32_t op, TypeId vector, uint8_t scalar_class,
                                                  Tier0Operation* row)
{
    const uint32_t bytes = (uint32_t)type_size(types, type_unqualified(type_strip_aligned(types, vector)));
    return check_vector_lower_baseline_piece(types, target, op, vector, bytes, scalar_class, row);
}

// A shuffle's output element where it has no AVX-512 permute (#775): from a (0), from b (1) or undefined (2), and its
// index in that vector at the plan's element size.
#define BUSTER_CHECK_BASE_FROM_A 0
#define BUSTER_CHECK_BASE_FROM_B 1
#define BUSTER_CHECK_BASE_UNDEFINED 2
// The most elements a shuffle has (64 bytes of 1-byte elements: BUSTER_CHECK_VECTOR_SHUFFLE_LANES).
#define BUSTER_CHECK_BASE_LANES 64u

typedef struct CheckVectorBaselineShuffle CheckVectorBaselineShuffle;
struct CheckVectorBaselineShuffle
{
    uint8_t from[BUSTER_CHECK_BASE_LANES];
    uint8_t index[BUSTER_CHECK_BASE_LANES];
    // The plan's element bytes (1, 2, 4 or 8) and elements, the output's.
    uint32_t element;
    uint32_t lanes;
};

// A shuffle at twice its element size where every aligned pair of elements is one wider element, or undefined (an
// undefined half takes the other's): the intrinsics' shuffles of bytes and words that move whole words and dwords.
static bool check_vector_baseline_widen(CheckVectorBaselineShuffle* plan)
{
    bool widened = plan->element < 8 && plan->lanes % 2 == 0;
    for (uint32_t j = 0; widened && j < plan->lanes / 2; j += 1)
    {
        const uint8_t f0 = plan->from[2 * j];
        const uint8_t f1 = plan->from[2 * j + 1];
        const uint8_t i0 = plan->index[2 * j];
        const uint8_t i1 = plan->index[2 * j + 1];
        widened = f0 == BUSTER_CHECK_BASE_UNDEFINED ? (f1 == BUSTER_CHECK_BASE_UNDEFINED || i1 % 2 == 1) :
                  f1 == BUSTER_CHECK_BASE_UNDEFINED ? i0 % 2 == 0 : f0 == f1 && i0 % 2 == 0 && i1 == i0 + 1;
    }
    if (widened)
    {
        for (uint32_t j = 0; j < plan->lanes / 2; j += 1)
        {
            const bool first = plan->from[2 * j] != BUSTER_CHECK_BASE_UNDEFINED;
            plan->from[j] = first ? plan->from[2 * j] : plan->from[2 * j + 1];
            plan->index[j] = (uint8_t)((first ? plan->index[2 * j] : plan->index[2 * j + 1]) / 2);
        }
        plan->element *= 2;
        plan->lanes /= 2;
    }
    return widened;
}

// Whether every defined output element is `vector`'s.
static bool check_vector_baseline_from_one(const CheckVectorBaselineShuffle* plan, uint8_t vector, uint32_t first, uint32_t count)
{
    bool one = true;
    for (uint32_t i = first; i < first + count && i < plan->lanes; i += 1)
    {
        one &= plan->from[i] == vector || plan->from[i] == BUSTER_CHECK_BASE_UNDEFINED;
    }
    return one;
}

// Whether the shuffle interleaves the low (`high` false) or high halves of x and y, elementwise: an unpack's pattern.
static bool check_vector_baseline_interleaves(const CheckVectorBaselineShuffle* plan, uint32_t lanes, bool high, uint8_t x, uint8_t y)
{
    bool interleaves = true;
    for (uint32_t i = 0; i < plan->lanes && i < lanes; i += 1)
    {
        const uint8_t from = i % 2 == 0 ? x : y;
        const uint32_t index = i / 2 + (high ? lanes / 2 : 0);
        interleaves &= plan->from[i] == BUSTER_CHECK_BASE_UNDEFINED || (plan->from[i] == from && plan->index[i] == index);
    }
    return interleaves;
}

// The record of a two-source form with an immediate into `to`, of `first` and `second`: a legacy form's destination
// tied to `first` by a copy, through `scratch` where `to` is `second`.
static void check_vector_baseline_pair(CheckVectorBaseline* context, uint32_t form, uint8_t to, uint8_t first, uint8_t second, uint8_t scratch,
                                       uint8_t immediate)
{
    const bool vex = x86_form_description(form)->family == BUSTER_X86_FAMILY_VEX;
    const uint8_t into = !vex && to != first && to == second ? scratch : to;
    check_vector_baseline_binary(context, form, into, first, second);
    if (context->valid && context->row->record_count != 0)
    {
        context->row->records[context->row->record_count - 1].immediate = immediate;
    }
    check_vector_baseline_copy(context, to, into);
}

// A shuffle of dwords from one or two vectors into VECTOR(0): PSHUFD's of one, SHUFPS's of two whose halves each take
// one vector's (MOVHLPS's and MOVLHPS's shuffles), an unpack's, or two shuffles: SHUFPS's of the two elements each
// vector gives, then PSHUFD's of those, or, where one vector gives three, SHUFPS's of the other's and the one beside it,
// then SHUFPS's of that and the three's vector, as clang's code computes v4i32 and v4f32 shuffles.
static void check_vector_baseline_dwords(CheckVectorBaseline* context, const CheckVectorBaselineShuffle* plan, const uint8_t* roles, uint8_t scratch)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint32_t shufps = context->avx ? X86_FORM_VSHUFPS_X_X_XM128_IMM8 : X86_FORM_SHUFPS_X_XM128_IMM8;
    uint8_t picked[2][4] = { { 0 } };
    uint32_t counts[2] = { 0, 0 };
    for (uint32_t i = 0; i < plan->lanes; i += 1)
    {
        const uint8_t from = plan->from[i];
        bool seen = from == BUSTER_CHECK_BASE_UNDEFINED;
        for (uint32_t k = 0; !seen && k < counts[from]; k += 1)
        {
            seen = picked[from][k] == plan->index[i];
        }
        if (!seen)
        {
            picked[from][counts[from]] = plan->index[i];
            counts[from] += 1;
        }
    }
    const uint8_t low = check_vector_baseline_from_one(plan, BUSTER_CHECK_BASE_FROM_A, 0, 2) ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
    const uint8_t high = check_vector_baseline_from_one(plan, BUSTER_CHECK_BASE_FROM_A, 2, 2) ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
    if (counts[BUSTER_CHECK_BASE_FROM_B] == 0 || counts[BUSTER_CHECK_BASE_FROM_A] == 0)
    {
        const uint8_t from = counts[BUSTER_CHECK_BASE_FROM_B] == 0 ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
        uint8_t immediate = 0;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            immediate |= (uint8_t)((i < plan->lanes && plan->from[i] != BUSTER_CHECK_BASE_UNDEFINED ? plan->index[i] & 3 : i) << (2 * i));
        }
        check_vector_baseline_shuffle(context, v0, roles[from], immediate);
    }
    else if (check_vector_baseline_from_one(plan, low, 0, 2) && check_vector_baseline_from_one(plan, high, 2, 2))
    {
        uint8_t immediate = 0;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            immediate |= (uint8_t)((i < plan->lanes && plan->from[i] != BUSTER_CHECK_BASE_UNDEFINED ? plan->index[i] & 3 : 0) << (2 * i));
        }
        check_vector_baseline_pair(context, shufps, v0, roles[low], roles[high], scratch, immediate);
    }
    else if (counts[0] <= 2 && counts[1] <= 2)
    {
        // SHUFPS's of a's elements into the low half, b's into the high one, then PSHUFD's of those into their places.
        uint8_t gather = 0;
        for (uint32_t k = 0; k < 4; k += 1)
        {
            const uint32_t from = k / 2;
            gather |= (uint8_t)((k % 2 < counts[from] ? picked[from][k % 2] : picked[from][0]) << (2 * k));
        }
        check_vector_baseline_pair(context, shufps, v0, roles[0], roles[1], scratch, gather);
        uint8_t place = 0;
        for (uint32_t i = 0; i < 4; i += 1)
        {
            uint32_t slot = i;
            if (i < plan->lanes && plan->from[i] != BUSTER_CHECK_BASE_UNDEFINED)
            {
                const uint32_t from = plan->from[i];
                slot = 2 * from + (counts[from] == 2 && picked[from][1] == plan->index[i] ? 1u : 0u);
            }
            place |= (uint8_t)(slot << (2 * i));
        }
        check_vector_baseline_shuffle(context, v0, v0, place);
    }
    else
    {
        // Three elements of x and one of y, at `at`: SHUFPS's of y's beside the element of x in its half, into scratch
        // ([x, x, y, y]), then SHUFPS's of x's half and that one's in their order.
        const uint8_t x = counts[0] == 3 ? 0 : 1;
        const uint8_t y = (uint8_t)(1 - x);
        uint32_t at = 0;
        while (plan->from[at] != y)
        {
            at += 1;
        }
        const uint32_t beside = at ^ 1;
        const uint8_t other = plan->from[beside] == x ? plan->index[beside] : 0;
        const uint8_t pair = (uint8_t)(other | other << 2 | plan->index[at] << 4 | plan->index[at] << 6);
        check_vector_baseline_pair(context, shufps, scratch, roles[x], roles[y], scratch, pair);
        // In the half of y's element, scratch's slot 2 gives y's, 0 the element of x beside it.
        const uint8_t own = (uint8_t)((at % 2 == 0 ? 2 : 0) | (at % 2 == 0 ? 0 : 2) << 2);
        uint8_t kept = 0;
        const uint32_t kept_half = at < 2 ? 2 : 0;
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const uint32_t i = kept_half + k;
            kept |= (uint8_t)((plan->from[i] == x ? plan->index[i] : 0) << (2 * k));
        }
        const uint8_t immediate = at < 2 ? (uint8_t)(own | kept << 4) : (uint8_t)(kept | own << 4);
        check_vector_baseline_pair(context, shufps, v0, at < 2 ? scratch : roles[x], at < 2 ? roles[x] : scratch, scratch, immediate);
    }
}

// The row of __builtin_shufflevector(a, b, indices...) where the target hasn't AVX-512F and AVX-512VL (#775), or the
// reason it has none: its records only (the caller gives the row its operands, a in VECTOR(0) and b in VECTOR(1), and
// `constant_role` the next one, which a row with a constant takes: `constant` its bytes and `constant_bytes` their
// count, 0 without one). The shuffle is planned at the widest element size its pairs of elements allow, then:
//   - in 16 bytes, of 8-byte elements by PSHUFD's or SHUFPD's, of 4-byte ones by check_vector_baseline_dwords, of
//     2- and 1-byte ones by an unpack's, PSHUFLW's and PSHUFHW's of one vector's halves, or with SSSE3 by PSHUFB's of
//     each vector with the constant's indices (the other's elements zeroed by their top bits), ORed;
//   - with AVX, a 32-byte vector of two 16-byte ones by VINSERTF128, one's high half by VEXTRACTF128, and with AVX2 a
//     shuffle of 32-byte vectors' dwords by VPERMD's of each and VPBLENDD's of the two.
// Others are refused by name, as clang -O0 computes them in sequences no row takes.
static const char* check_vector_baseline_shuffle_row(const X86Target* target, uint32_t element, uint32_t in_lanes, uint32_t in_bytes, uint32_t out_lanes,
                                                     uint32_t out_bytes, const uint32_t* indices, uint8_t constant_role, Tier0Operation* row,
                                                     uint8_t* constant, uint32_t* constant_bytes)
{
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const bool avx2 = (target->features & BUSTER_X86_FEATURE(AVX2)) != 0;
    const bool ssse3 = (target->features & BUSTER_X86_FEATURE(SSSE3)) != 0;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t roles[2] = { v0, v1 };
    // A spare register past the operands, beside the constant where a row takes one (PSHUFB's) and in its place elsewhere.
    const uint8_t scratch = constant_role;
    const char* reason = NULL;
    CheckVectorBaseline context = { row, 16, avx, true };
    CheckVectorBaselineShuffle plan = { .element = element, .lanes = out_lanes };
    *constant_bytes = 0;
    for (uint32_t i = 0; i < out_lanes && i < BUSTER_CHECK_BASE_LANES; i += 1)
    {
        const uint32_t k = indices[i];
        plan.from[i] = k == UINT32_MAX ? BUSTER_CHECK_BASE_UNDEFINED : k < in_lanes ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
        plan.index[i] = (uint8_t)(k == UINT32_MAX ? 0 : k < in_lanes ? k : k - in_lanes);
    }
    bool widened = true;
    while (widened)
    {
        widened = check_vector_baseline_widen(&plan);
    }
    const uint32_t wide = in_bytes > out_bytes ? in_bytes : out_bytes;
    const uint32_t half = 16 / plan.element;
    // A 32-byte vector of a's 16 bytes, then a's or b's (`upper`); a 16-byte one of a's high half; a splat of an element 0.
    uint8_t upper = BUSTER_CHECK_BASE_UNDEFINED;
    bool concatenates = in_bytes == 16 && out_bytes == 32;
    bool extracts_high = in_bytes == 32 && out_bytes == 16;
    bool splats = true;
    uint8_t splat = BUSTER_CHECK_BASE_UNDEFINED;
    for (uint32_t i = 0; i < plan.lanes; i += 1)
    {
        const uint8_t from = plan.from[i];
        const bool defined = from != BUSTER_CHECK_BASE_UNDEFINED;
        // The upper half's source is its first defined element's, which every other defined one there must share.
        upper = i >= half && defined && upper == BUSTER_CHECK_BASE_UNDEFINED ? from : upper;
        concatenates &= !defined || ((i < half ? from == BUSTER_CHECK_BASE_FROM_A : from == upper) && plan.index[i] == i % half);
        extracts_high &= !defined || (from == BUSTER_CHECK_BASE_FROM_A && plan.index[i] == i + half);
        splats &= !defined || ((splat == BUSTER_CHECK_BASE_UNDEFINED || from == splat) && plan.index[i] == 0);
        splat = defined ? from : splat;
    }
    // AVX2's unpacks interleave each 16-byte lane's own halves.
    uint32_t lane_unpack = 0;
    uint8_t lane_x = 0;
    uint8_t lane_y = 0;
    for (uint32_t k = 0; wide > 16 && avx2 && in_bytes == 32 && out_bytes == 32 && plan.element <= 2 && lane_unpack == 0 && k < 8; k += 1)
    {
        const bool high = (k & 4) != 0;
        bool interleaves = true;
        for (uint32_t i = 0; i < plan.lanes; i += 1)
        {
            const uint32_t lane = i / half;
            const uint8_t from = (uint8_t)(i % 2 == 0 ? (k & 1) : (k >> 1 & 1));
            const uint32_t index = lane * half + (i % half) / 2 + (high ? half / 2 : 0);
            interleaves &= plan.from[i] == BUSTER_CHECK_BASE_UNDEFINED || (plan.from[i] == from && plan.index[i] == index);
        }
        if (interleaves)
        {
            lane_x = (uint8_t)(k & 1);
            lane_y = (uint8_t)(k >> 1 & 1);
            lane_unpack = plan.element == 1 ? (high ? X86_FORM_VPUNPCKHBW_Y_Y_YM256 : X86_FORM_VPUNPCKLBW_Y_Y_YM256) :
                                              (high ? X86_FORM_VPUNPCKHWD_Y_Y_YM256 : X86_FORM_VPUNPCKLWD_Y_Y_YM256);
        }
    }
    if (splats && splat != BUSTER_CHECK_BASE_UNDEFINED && avx2 && plan.element <= 2)
    {
        // VPBROADCASTB's or VPBROADCASTW's of the vector's first element.
        const uint32_t form = plan.element == 1 ? (out_bytes > 16 ? X86_FORM_VPBROADCASTB_Y_XM8 : X86_FORM_VPBROADCASTB_X_XM8) :
                                                  (out_bytes > 16 ? X86_FORM_VPBROADCASTW_Y_XM16 : X86_FORM_VPBROADCASTW_X_XM16);
        check_vector_baseline_put(&context, check_vector_lower_record(form, v0, 0, roles[splat]));
    }
    else if (lane_unpack != 0)
    {
        check_vector_baseline_put(&context, check_vector_lower_record(lane_unpack, v0, roles[lane_x], roles[lane_y]));
    }
    else if (wide > 16 && avx && concatenates)
    {
        X86Instruction record = check_vector_lower_record(X86_FORM_VINSERTF128_Y_Y_XM128_IMM8, v0, v0, roles[upper == BUSTER_CHECK_BASE_FROM_B ? 1 : 0]);
        record.immediate = 1;
        check_vector_baseline_put(&context, record);
    }
    else if (wide > 16 && avx && extracts_high)
    {
        X86Instruction record = check_vector_lower_record(X86_FORM_VEXTRACTF128_XM128_Y_IMM8, v0, 0, v0);
        record.immediate = 1;
        check_vector_baseline_put(&context, record);
    }
    else if (wide > 16 && avx2 && plan.element >= 4)
    {
        // VPERMD's of each vector by the constant's dword indices, then VPBLENDD's of b's where they are its.
        const uint32_t split = plan.element / 4;
        uint8_t blend = 0;
        bool reads_b = false;
        memset(constant, 0, 32);
        for (uint32_t i = 0; i < 8; i += 1)
        {
            const uint32_t lane = i / split;
            const uint8_t from = plan.from[lane];
            constant[4 * i] = (uint8_t)((plan.index[lane] * split + i % split) & 7);
            blend |= (uint8_t)(from == BUSTER_CHECK_BASE_FROM_B ? 1u << i : 0u);
            reads_b |= from == BUSTER_CHECK_BASE_FROM_B;
        }
        const bool only_b = check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_B, 0, plan.lanes);
        check_vector_baseline_put(&context, check_vector_lower_record(X86_FORM_VPERMD_Y_Y_YM256, v0, constant_role, only_b ? v1 : v0));
        if (reads_b && !only_b)
        {
            check_vector_baseline_put(&context, check_vector_lower_record(X86_FORM_VPERMD_Y_Y_YM256, v1, constant_role, v1));
            X86Instruction record = check_vector_lower_record(X86_FORM_VPBLENDD_Y_Y_YM256_IMM8, v0, v0, v1);
            record.immediate = blend;
            check_vector_baseline_put(&context, record);
        }
        *constant_bytes = 32;
    }
    else if (wide > 16)
    {
        reason = !avx2 ? "vector shuffles of 32-byte vectors without AVX2" : "vector shuffles of 32-byte vectors of 8- and 16-bit elements without AVX-512";
    }
    else if (plan.element == 8)
    {
        const bool one = check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_A, 0, 2) || check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_B, 0, 2);
        if (one)
        {
            // PSHUFD's of the qwords' dwords.
            const uint8_t from = check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_A, 0, 2) ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
            uint8_t immediate = 0;
            for (uint32_t i = 0; i < 2; i += 1)
            {
                const uint32_t q = i < plan.lanes && plan.from[i] != BUSTER_CHECK_BASE_UNDEFINED ? plan.index[i] & 1 : i;
                immediate |= (uint8_t)((2 * q | (2 * q + 1) << 2) << (4 * i));
            }
            check_vector_baseline_shuffle(&context, v0, roles[from], immediate);
        }
        else
        {
            const uint32_t shufpd = avx ? X86_FORM_VSHUFPD_X_X_XM128_IMM8 : X86_FORM_SHUFPD_X_XM128_IMM8;
            const uint8_t immediate = (uint8_t)((plan.index[0] & 1) | (plan.index[1] & 1) << 1);
            check_vector_baseline_pair(&context, shufpd, v0, roles[plan.from[0]], roles[plan.from[1]], scratch, immediate);
        }
    }
    else if (plan.element == 4)
    {
        check_vector_baseline_dwords(&context, &plan, roles, scratch);
    }
    else
    {
        // An unpack of x's and y's low or high halves, or PSHUFLW's and PSHUFHW's of one vector's own halves.
        const uint32_t lanes = 16 / plan.element;
        uint32_t unpack = 0;
        uint8_t x = 0;
        uint8_t y = 0;
        for (uint32_t k = 0; unpack == 0 && k < 8; k += 1)
        {
            const bool high = (k & 4) != 0;
            x = (uint8_t)(k & 1);
            y = (uint8_t)(k >> 1 & 1);
            if (check_vector_baseline_interleaves(&plan, lanes, high, x, y))
            {
                unpack = plan.element == 1 ? (high ? (avx ? X86_FORM_VPUNPCKHBW_X_X_XM128 : X86_FORM_PUNPCKHBW_X_XM128) :
                                                     (avx ? X86_FORM_VPUNPCKLBW_X_X_XM128 : X86_FORM_PUNPCKLBW_X_XM128)) :
                                             (high ? (avx ? X86_FORM_VPUNPCKHWD_X_X_XM128 : X86_FORM_PUNPCKHWD_X_XM128) :
                                                     (avx ? X86_FORM_VPUNPCKLWD_X_X_XM128 : X86_FORM_PUNPCKLWD_X_XM128));
            }
        }
        bool halves = plan.element == 2 && (check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_A, 0, 8) ||
                                            check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_B, 0, 8));
        for (uint32_t i = 0; halves && i < plan.lanes; i += 1)
        {
            halves = plan.from[i] == BUSTER_CHECK_BASE_UNDEFINED || plan.index[i] / 4 == i / 4;
        }
        if (unpack != 0)
        {
            check_vector_baseline_pair(&context, unpack, v0, roles[x], roles[y], scratch, 0);
        }
        else if (halves)
        {
            const uint8_t from = check_vector_baseline_from_one(&plan, BUSTER_CHECK_BASE_FROM_A, 0, 8) ? BUSTER_CHECK_BASE_FROM_A : BUSTER_CHECK_BASE_FROM_B;
            uint8_t immediates[2] = { 0, 0 };
            for (uint32_t i = 0; i < 8; i += 1)
            {
                const uint32_t word = i < plan.lanes && plan.from[i] != BUSTER_CHECK_BASE_UNDEFINED ? plan.index[i] % 4 : i % 4;
                immediates[i / 4] |= (uint8_t)(word << (2 * (i % 4)));
            }
            X86Instruction low = check_vector_lower_record(avx ? X86_FORM_VPSHUFLW_X_XM128_IMM8 : X86_FORM_PSHUFLW_X_XM128_IMM8, v0, 0, roles[from]);
            low.immediate = immediates[0];
            check_vector_baseline_put(&context, low);
            X86Instruction high = check_vector_lower_record(avx ? X86_FORM_VPSHUFHW_X_XM128_IMM8 : X86_FORM_PSHUFHW_X_XM128_IMM8, v0, 0, v0);
            high.immediate = immediates[1];
            check_vector_baseline_put(&context, high);
        }
        else if (ssse3)
        {
            // The constant's byte i: a's byte's index, or b's with the top bit set (PSHUFB zeroes a byte whose index has
            // it); b's PSHUFB takes the constant with each top bit flipped, by 0x80 in each byte (check_vector_baseline_compare's).
            const uint32_t pshufb = avx ? X86_FORM_VPSHUFB_X_X_XM128 : X86_FORM_PSHUFB_X_XM128;
            bool reads_a = false;
            bool reads_b = false;
            for (uint32_t i = 0; i < 16; i += 1)
            {
                const uint32_t lane = i / plan.element;
                const uint8_t from = lane < plan.lanes ? plan.from[lane] : BUSTER_CHECK_BASE_UNDEFINED;
                const uint8_t byte = (uint8_t)(plan.index[lane] * plan.element + i % plan.element);
                constant[i] = from == BUSTER_CHECK_BASE_FROM_A ? byte : (uint8_t)(0x80 | byte);
                reads_a |= from == BUSTER_CHECK_BASE_FROM_A;
                reads_b |= from == BUSTER_CHECK_BASE_FROM_B;
            }
            *constant_bytes = 16;
            if (reads_b)
            {
                const uint8_t flip = (uint8_t)(constant_role + 1);
                check_vector_baseline_ones(&context, flip);
                check_vector_baseline_shift(&context, flip, 2, 7);
                check_vector_baseline_binary(&context, avx ? X86_FORM_VPACKSSWB_X_X_XM128 : X86_FORM_PACKSSWB_X_XM128, flip, flip, flip);
                check_vector_baseline_binary(&context, avx ? X86_FORM_VXORPS_X_X_XM128 : X86_FORM_XORPS_X_XM128, flip, flip, constant_role);
                check_vector_baseline_binary(&context, pshufb, v1, v1, flip);
            }
            if (reads_a)
            {
                check_vector_baseline_binary(&context, pshufb, v0, v0, constant_role);
            }
            if (reads_a && reads_b)
            {
                check_vector_baseline_binary(&context, avx ? X86_FORM_VORPS_X_X_XM128 : X86_FORM_ORPS_X_XM128, v0, v0, v1);
            }
            else if (reads_b)
            {
                check_vector_baseline_copy(&context, v0, v1);
            }
        }
        else
        {
            reason = plan.element == 1 ? "vector shuffles of 8-bit elements without SSSE3" : "vector shuffles of 16-bit elements without SSSE3";
        }
    }
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    for (uint32_t r = 0; context.valid && r < row->record_count; r += 1)
    {
        const uint64_t needed = x86_target_requirement(row->records[r].form);
        context.valid = needed != 0 && (needed & ~available) == 0;
    }
    if (reason == NULL && !context.valid)
    {
        reason = "vector shuffles unavailable at this target";
    }
    return reason;
}

#undef BUSTER_CHECK_BASE_FROM_A
#undef BUSTER_CHECK_BASE_FROM_B
#undef BUSTER_CHECK_BASE_UNDEFINED
#undef BUSTER_CHECK_BASE_LANES

// BUSTER_CHECK_BASE_PACKED and BUSTER_CHECK_BASE_UNARY are the conversions' too (c_check_program_vector_builtins.c).
