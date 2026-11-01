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

static void check_vector_baseline_shift(CheckVectorBaseline* context, uint8_t value, uint32_t element, uint8_t bits)
{
    const bool wide = context->bytes > 16;
    const uint32_t form = element == 2 ? (wide ? X86_FORM_VPSLLW_Y_Y_IMM8 : context->avx ? X86_FORM_VPSLLW_X_X_IMM8 : X86_FORM_PSLLW_X_IMM8) :
                          element == 4 ? (wide ? X86_FORM_VPSLLD_Y_Y_IMM8 : context->avx ? X86_FORM_VPSLLD_X_X_IMM8 : X86_FORM_PSLLD_X_IMM8) :
                                         (wide ? X86_FORM_VPSLLQ_Y_Y_IMM8 : context->avx ? X86_FORM_VPSLLQ_X_X_IMM8 : X86_FORM_PSLLQ_X_IMM8);
    X86Instruction record = check_vector_lower_record(form, 0, context->avx ? value : 0, value);
    record.immediate = bits;
    check_vector_baseline_put(context, record);
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

static const char* check_vector_lower_baseline_row(const TypeTable* types, const X86Target* target, uint32_t op, TypeId vector, uint8_t scalar_class,
                                                  Tier0Operation* row)
{
    const CheckVectorElement element = check_vector_lower_element(types, vector);
    const uint32_t bytes = (uint32_t)type_size(types, type_unqualified(type_strip_aligned(types, vector)));
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
                 op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT ? "vector shifts unavailable at this target" :
                 op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER ? "vector division of integers" :
                 compare ? "vector comparison unavailable at this target" : "vector operator unavailable at this target";
    }
    return reason;
}

#undef BUSTER_CHECK_BASE_PACKED
