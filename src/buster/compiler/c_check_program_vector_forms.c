// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Reviewed, unmasked equivalents for the pre-AVX-512 builtin rows. This is not a prefix rewrite: opmask compares,
// broadcast, suppression of exceptions, rounding and implicit-register forms are deliberately not translated.
// Legacy destructive destinations are tied explicitly, with a copy only when it cannot overwrite another source.

static uint32_t check_vector_baseline_form(uint32_t form, bool avx)
{
    uint32_t result = form;
    switch (form)
    {
#define BUSTER_BASE_FORM(evex, vex, legacy) case X86_FORM_##evex: result = avx ? X86_FORM_##vex : X86_FORM_##legacy; break;
#define BUSTER_BASE_BINARY(name) BUSTER_BASE_FORM(V##name##_X_X_XM128_EVEX, V##name##_X_X_XM128, name##_X_XM128) \
    BUSTER_BASE_FORM(V##name##_Y_Y_YM256_EVEX, V##name##_Y_Y_YM256, NONE)
        BUSTER_BASE_BINARY(PACKSSWB)
        BUSTER_BASE_BINARY(PACKSSDW)
        BUSTER_BASE_BINARY(PACKUSWB)
        BUSTER_BASE_BINARY(PMADDWD)
        BUSTER_BASE_BINARY(PMULHW)
        BUSTER_BASE_BINARY(PMULHUW)
        BUSTER_BASE_BINARY(PMULUDQ)
        BUSTER_BASE_BINARY(PMULDQ)
        BUSTER_BASE_BINARY(MAXPS)
        BUSTER_BASE_BINARY(MINPS)
        BUSTER_BASE_BINARY(MAXPD)
        BUSTER_BASE_BINARY(MINPD)
        BUSTER_BASE_FORM(VSHUFPS_X_X_XM128_IMM8_EVEX, VSHUFPS_X_X_XM128_IMM8, SHUFPS_X_XM128_IMM8)
        BUSTER_BASE_FORM(VSHUFPD_X_X_XM128_IMM8, VSHUFPD_X_X_XM128_IMM8, SHUFPD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VSHUFPD_X_X_XM128_IMM8_EVEX, VSHUFPD_X_X_XM128_IMM8, SHUFPD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFD_X_XM128_IMM8_EVEX, VPSHUFD_X_XM128_IMM8, PSHUFD_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFHW_X_XM128_IMM8_EVEX, VPSHUFHW_X_XM128_IMM8, PSHUFHW_X_XM128_IMM8)
        BUSTER_BASE_FORM(VPSHUFLW_X_XM128_IMM8_EVEX, VPSHUFLW_X_XM128_IMM8, PSHUFLW_X_XM128_IMM8)
        BUSTER_BASE_FORM(VCVTPS2DQ_X_XM128_EVEX, VCVTPS2DQ_X_XM128, CVTPS2DQ_X_XM128)
        BUSTER_BASE_FORM(VCVTTPS2DQ_X_XM128_EVEX, VCVTTPS2DQ_X_XM128, CVTTPS2DQ_X_XM128)
        BUSTER_BASE_FORM(VCVTDQ2PS_X_XM128_EVEX, VCVTDQ2PS_X_XM128, CVTDQ2PS_X_XM128)
        BUSTER_BASE_FORM(VMOVD_X_RM32_EVEX, VMOVD_X_RM32, MOVD_X_RM32)
        BUSTER_BASE_FORM(VMOVD_RM32_X_EVEX, VMOVD_RM32_X, MOVD_RM32_X)
        BUSTER_BASE_FORM(VMOVQ_RM64_X_EVEX, VMOVQ_RM64_X, MOVQ_RM64_X)
        BUSTER_BASE_FORM(VMOVQ_X_XM64_EVEX, VMOVQ_X_XM64, MOVQ_X_XM64)
        BUSTER_BASE_FORM(VMOVQ_X_RM64_EVEX, VMOVQ_X_RM64, MOVQ_X_RM64)
        BUSTER_BASE_FORM(VPEXTRW_R32_X_IMM8_EVEX, VPEXTRW_R32_X_IMM8, PEXTRW_R32_X_IMM8)
        BUSTER_BASE_FORM(VPINSRW_X_X_RM32_IMM8_EVEX, VPINSRW_X_X_RM32_IMM8, PINSRW_X_RM32_IMM8)
#define BUSTER_BASE_SHIFT(name) BUSTER_BASE_FORM(V##name##_X_X_XM128_EVEX, V##name##_X_X_XM128, name##_X_XM128) \
        BUSTER_BASE_FORM(V##name##_X_XM128_IMM8, V##name##_X_X_IMM8, name##_X_IMM8)
        BUSTER_BASE_SHIFT(PSLLW)
        BUSTER_BASE_SHIFT(PSLLD)
        BUSTER_BASE_SHIFT(PSLLQ)
        BUSTER_BASE_SHIFT(PSRLW)
        BUSTER_BASE_SHIFT(PSRLD)
        BUSTER_BASE_SHIFT(PSRLQ)
        BUSTER_BASE_SHIFT(PSRAW)
        BUSTER_BASE_SHIFT(PSRAD)
        BUSTER_BASE_FORM(VPSLLDQ_X_XM128_IMM8, VPSLLDQ_X_X_IMM8, PSLLDQ_X_IMM8)
        BUSTER_BASE_FORM(VPSRLDQ_X_XM128_IMM8, VPSRLDQ_X_X_IMM8, PSRLDQ_X_IMM8)
#undef BUSTER_BASE_SHIFT
#undef BUSTER_BASE_BINARY
#undef BUSTER_BASE_FORM
        default: break;
    }
    return result;
}

static bool check_vector_baseline_row(const X86Target* target, const Tier0Operation* input, Tier0Operation* output)
{
    *output = *input;
    output->record_count = 0;
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const uint32_t bytes = avx ? 32 : 16;
    const uint64_t available = target->features | BUSTER_X86_TARGET_KNOWN;
    bool valid = input->result_class != BUSTER_TYPED_CLASS_VECTOR || input->result_bytes <= bytes;
    for (uint32_t j = 0; j < input->operand_count; j += 1)
    {
        valid = valid && (input->operand_classes[j] != BUSTER_TYPED_CLASS_VECTOR || input->operand_bytes[j] <= bytes);
    }
    for (uint32_t i = 0; valid && i < input->record_count; i += 1)
    {
        X86Instruction record = input->records[i];
        const uint32_t form = check_vector_baseline_form(record.form, avx);
        const uint64_t required = x86_target_requirement(form);
        valid = form != X86_FORM_NONE && required != 0 && (required & ~available) == 0;
        if (valid && form != record.form)
        {
            const X86FormDescription* before = x86_form_description(record.form);
            const X86FormDescription* after = x86_form_description(form);
            valid = record.decorations == 0;
            if (valid && after->family == BUSTER_X86_FAMILY_LEGACY && before->vvvv != BUSTER_X86_OPERAND_NONE)
            {
                // Immediate packed shifts encode their destination in vvvv, unlike the other binary forms.
                const bool shift = before->reg == BUSTER_X86_OPERAND_NONE;
                const uint8_t to = shift ? record.vvvv : record.reg;
                const uint8_t from = shift ? record.rm : record.vvvv;
                if (to != from)
                {
                    valid = record.mode == BUSTER_X86_REGISTER_DIRECT && (shift || to != record.rm) &&
                            output->record_count + 1 < BUSTER_TIER0_OPERATION_RECORDS;
                    if (valid)
                    {
                        output->records[output->record_count++] = check_vector_lower_record(X86_FORM_MOVUPS_X_XM, to, 0, from);
                    }
                }
                record.vvvv = 0;
                if (shift)
                {
                    record.rm = to;
                }
            }
            record.form = (uint16_t)form;
        }
        valid = valid && output->record_count < BUSTER_TIER0_OPERATION_RECORDS;
        if (valid)
        {
            output->records[output->record_count++] = record;
        }
    }
    return valid;
}
