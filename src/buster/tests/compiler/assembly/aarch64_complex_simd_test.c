#include <buster/tests/compiler/assembly/aarch64_complex_simd_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.h>

static Target a64_complex_simd_test_target(void)
{
    return (Target){.cpu_arch = CPU_ARCH_AARCH64,
                    .cpu_model = CPU_MODEL_A64_APPLE_M1,
                    .os = OPERATING_SYSTEM_MACOS,
                    .cpu_features_explicit = true,
                    .cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1)};
}

static BusterA64ComplexSIMDArrangement a64_complex_simd_alternate_arrangement(BusterA64SemanticVMValue value)
{
    BusterA64ComplexSIMDArrangement result;
    if (value.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR)
    {
        result = value.aux == BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_B ? BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_H : BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_B;
    }
    else
    {
        result = value.aux == BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B ? BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8B : BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B;
    }

    return result;
}

static u32 a64_complex_simd_audit_mix(u32 value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static bool a64_complex_simd_audit_canonical_form(u64 digest, u32* form_index, BusterAarch64CanonicalFormInfo* result)
{
    if (!form_index || !result)
    {
        return false;
    }
    u32 matches = 0;
    for (u32 index = 0; index < buster_aarch64_canonical_form_count(); index += 1)
    {
        BusterAarch64CanonicalFormInfo candidate = {0};
        if (!buster_aarch64_canonical_form(index, &candidate) || candidate.arm_row_digest != digest)
        {
            continue;
        }
        matches += 1;
        *form_index = index;
        *result = candidate;
    }
    return matches == 1;
}

static bool a64_complex_simd_audit_word(Target target, u32 row_index, u64 digest, u32 canonical_index, u32 word, u32* first_word)
{
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS || canonical.form_index != canonical_index ||
        canonical.arm_row_digest != digest)
    {
        return false;
    }
    BusterA64ComplexSIMDResult typed = {0};
    if (buster_a64_complex_simd_decode_row(target, row_index, word, &typed) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK)
    {
        return false;
    }
    BusterA64ComplexSIMDInstruction instruction = {.row_index = row_index, .operand_count = (u8)typed.operand_count};
    instruction.raw_fields_valid = typed.raw_fields_valid;
    instruction.raw_fields = typed.raw_fields;
    for (u32 index = 0; index < typed.operand_count; index += 1)
    {
        instruction.operands[index] = typed.operands[index];
    }
    u32 reencoded = 0;
    if (buster_a64_complex_simd_encode(target, &instruction, &reencoded) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK || reencoded != word)
    {
        return false;
    }
    if (first_word && *first_word == UINT32_MAX)
    {
        *first_word = word;
    }
    return true;
}

static bool a64_complex_simd_audit_try_word(Target target, u32 row_index, u64 digest, u32 canonical_index, u32 word, u32* tried, u32* tried_count,
                                           u32* legal_count, u32* first_word)
{
    if (!tried || !tried_count || !legal_count || *tried_count >= 1024u)
    {
        return false;
    }
    for (u32 index = 0; index < *tried_count; index += 1)
    {
        if (tried[index] == word)
        {
            return true;
        }
    }
    tried[*tried_count] = word;
    *tried_count += 1;
    if (a64_complex_simd_audit_word(target, row_index, digest, canonical_index, word, first_word))
    {
        *legal_count += 1;
    }
    return true;
}

static bool a64_complex_simd_audit_row(Target target, u32 row_index, BusterA64ComplexSIMDRowInfo row, u32 canonical_index,
                                      BusterAarch64CanonicalFormInfo canonical, u32* legal_count, u32* first_word)
{
    if (!legal_count || !first_word || canonical.field_count > 32)
    {
        return false;
    }
    *legal_count = 0;
    *first_word = UINT32_MAX;
    u32 base_fields[32] = {0};
    if (!buster_aarch64_canonical_raw_decode(canonical_index, canonical.representative_word, base_fields, canonical.field_count))
    {
        return false;
    }
    u32 tried[1024] = {0};
    u32 tried_count = 0;
    a64_complex_simd_audit_try_word(target, row_index, row.source_digest, canonical_index, canonical.representative_word, tried, &tried_count, legal_count,
                                   first_word);
    for (u32 field_index = 0; field_index < canonical.field_count; field_index += 1)
    {
        BusterAarch64CanonicalFieldInfo field = {0};
        if (!buster_aarch64_canonical_field(canonical_index, field_index, &field) || field.width == 0)
        {
            return false;
        }
        u32 maximum = field.source_mask;
        u32 samples[16] = {0};
        u32 sample_count = 0;
        if (field.width <= 4)
        {
            maximum = field.width == 32 ? UINT32_MAX : ((UINT32_C(1) << field.width) - 1u);
            for (u32 value = 0; value <= maximum; value += 1)
            {
                samples[sample_count++] = value;
            }
        }
        else
        {
            samples[sample_count++] = base_fields[field_index];
            samples[sample_count++] = 0;
            samples[sample_count++] = maximum;
            if (base_fields[field_index] != 0)
            {
                samples[sample_count++] = base_fields[field_index] - 1;
            }
            if (base_fields[field_index] != maximum)
            {
                samples[sample_count++] = base_fields[field_index] + 1;
            }
            for (u32 random_index = 0; random_index < 4; random_index += 1)
            {
                samples[sample_count++] = a64_complex_simd_audit_mix(row_index * 131u + field_index * 17u + random_index) & maximum;
            }
        }
        for (u32 sample_index = 0; sample_index < sample_count; sample_index += 1)
        {
            u32 candidate_fields[32] = {0};
            for (u32 index = 0; index < canonical.field_count; index += 1)
            {
                candidate_fields[index] = base_fields[index];
            }
            candidate_fields[field_index] = samples[sample_index];
            u32 word = 0;
            if (buster_aarch64_canonical_raw_encode(canonical_index, candidate_fields, canonical.field_count, &word))
            {
                a64_complex_simd_audit_try_word(target, row_index, row.source_digest, canonical_index, word, tried, &tried_count, legal_count, first_word);
            }
        }
    }
    return true;
}

UnitTestResult aarch64_complex_simd_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Target target = a64_complex_simd_test_target();
    BUSTER_TEST(arguments, buster_a64_complex_simd_schema_version() == 1);
    BUSTER_TEST(arguments, buster_a64_complex_simd_row_count() == 345);
    BUSTER_TEST(arguments, buster_a64_complex_simd_transform_row_count() == 206);
    BUSTER_TEST(arguments, buster_a64_complex_simd_max_operands() == 8);
    BUSTER_TEST(arguments, buster_a64_complex_simd_validate());
    BusterA64ComplexSIMDRowInfo row = {0};
    BUSTER_TEST(arguments, buster_a64_complex_simd_row(0, &row));
    BUSTER_TEST(arguments, row.semantic_form_id == 6 && row.operand_count == 7);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_list(31, 5, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_lane(0, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_4S, 4).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BusterA64SemanticVMValue vector64 = buster_a64_complex_simd_value_vector(31, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8B);
    BusterA64SemanticVMValue vector128 = buster_a64_complex_simd_value_vector(31, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B);
    BUSTER_TEST(arguments, vector64.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && vector64.width == 64);
    BUSTER_TEST(arguments, vector128.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && vector128.width == 128);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_vector(0, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_scalar(0, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_16B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_scalar(0, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_S).width == 32);
    BUSTER_TEST(arguments, buster_a64_complex_simd_value_list(31, 4, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8B).width == 64);
    BusterA64SemanticVMValue lane7 = buster_a64_complex_simd_value_lane(31, BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_8B, 7);
    BUSTER_TEST(arguments, lane7.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE && lane7.aux2 == 7);

    /* Every dynamic Ta/Tb/Va/Vb relation is generated from syntax adjacency.
     * Decode the canonical representative for every row, assert that the
     * typed register arrangement equals its exact selector, and reject a
     * deliberately cross-arranged register without changing the output word. */
    bool arrangement_bindings_ok = true;
    bool cross_arrangement_rejected = true;
    u32 arrangement_binding_count = 0;
    for (u32 row_index = 0; row_index < buster_a64_complex_simd_row_count(); row_index += 1)
    {
        BusterA64ComplexSIMDRowInfo binding_row = {0};
        BusterAarch64CanonicalFormInfo binding_form = {0};
        u32 canonical_index = UINT32_MAX;
        if (!buster_a64_complex_simd_row(row_index, &binding_row) ||
            !a64_complex_simd_audit_canonical_form(binding_row.source_digest, &canonical_index, &binding_form))
        {
            arrangement_bindings_ok = false;
            continue;
        }
        for (u32 operand_index = 0; operand_index < binding_row.operand_count; operand_index += 1)
        {
            BusterA64ComplexSIMDArrangementBinding binding = {0};
            if (buster_a64_complex_simd_arrangement_binding(row_index, operand_index, &binding))
            {
                arrangement_binding_count += 1;
            }
        }
        u32 binding_word = binding_form.representative_word;
        BusterA64ComplexSIMDResult binding_decoded = {0};
        if (buster_a64_complex_simd_decode_row(target, row_index, binding_word, &binding_decoded) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK)
        {
            u32 legal_count = 0;
            binding_word = UINT32_MAX;
            if (!a64_complex_simd_audit_row(target, row_index, binding_row, canonical_index, binding_form, &legal_count, &binding_word) || legal_count == 0 ||
                binding_word == UINT32_MAX ||
                buster_a64_complex_simd_decode_row(target, row_index, binding_word, &binding_decoded) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK)
            {
                arrangement_bindings_ok = false;
                continue;
            }
        }
        for (u32 operand_index = 0; operand_index < binding_row.operand_count; operand_index += 1)
        {
            BusterA64ComplexSIMDArrangementBinding binding = {0};
            if (!buster_a64_complex_simd_arrangement_binding(row_index, operand_index, &binding))
            {
                continue;
            }
            bool binding_ok = binding.selector_index < binding_decoded.operand_count &&
                                      ((binding.direction == 1 && binding.selector_index == operand_index + 1) ||
                                       (binding.direction == -1 && operand_index != 0 &&
                                        (u32)binding.selector_index + 1u == operand_index)) &&
                                      binding_decoded.operands[binding.selector_index].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT &&
                                      binding_decoded.operands[operand_index].aux == binding_decoded.operands[binding.selector_index].aux;
            arrangement_bindings_ok = arrangement_bindings_ok && binding_ok;
            if (binding.selector_index >= binding_decoded.operand_count)
            {
                cross_arrangement_rejected = false;
                continue;
            }
            BusterA64ComplexSIMDInstruction mismatched = {.row_index = row_index, .operand_count = (u8)binding_decoded.operand_count};
            for (u32 index = 0; index < binding_decoded.operand_count; index += 1)
            {
                mismatched.operands[index] = binding_decoded.operands[index];
            }
            mismatched.operands[operand_index].aux =
                (u32)a64_complex_simd_alternate_arrangement(mismatched.operands[operand_index]);
            u32 preserved_word = UINT32_C(0xa5a5a5a5);
            BusterA64ComplexSIMDStatus mismatch_status = buster_a64_complex_simd_encode(target, &mismatched, &preserved_word);
            cross_arrangement_rejected =
                cross_arrangement_rejected && mismatch_status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK && preserved_word == UINT32_C(0xa5a5a5a5);
        }
    }
    BUSTER_TEST(arguments, arrangement_binding_count == buster_a64_complex_simd_arrangement_binding_count());
    BUSTER_TEST(arguments, arrangement_bindings_ok);
    BUSTER_TEST(arguments, cross_arrangement_rejected);

    bool audit_join = true;
    bool audit_all_exercised = true;
    u32 audit_rows_exercised = 0;
    u32 audit_legal_total = 0;
    u32 baseline_word = UINT32_MAX;
    for (u32 audit_index = 0; audit_index < buster_a64_complex_simd_row_count(); audit_index += 1)
    {
        BusterA64ComplexSIMDRowInfo audit_row = {0};
        BusterAarch64CanonicalFormInfo audit_form = {0};
        BusterA64SemanticForm audit_semantic_form = {0};
        u32 canonical_index = UINT32_MAX;
        bool row_found = buster_a64_complex_simd_row(audit_index, &audit_row) &&
                         a64_complex_simd_audit_canonical_form(audit_row.source_digest, &canonical_index, &audit_form) &&
                         buster_a64_semantic_form(audit_row.semantic_form_id, &audit_semantic_form);
        BusterAarch64CanonicalDecodeResult representative_decode = {0};
        bool representative_join =
            row_found &&
            buster_aarch64_canonical_decode(target, audit_form.representative_word, &representative_decode) == BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS &&
            representative_decode.form_index == canonical_index && representative_decode.arm_row_digest == audit_row.source_digest;
        audit_join = audit_join && representative_join && audit_form.arm_row_digest == audit_row.source_digest &&
                     audit_semantic_form.source_digest == audit_row.source_digest;
        u32 legal_count = 0;
        u32 first_legal_word = UINT32_MAX;
        bool row_audited = row_found && a64_complex_simd_audit_row(target, audit_index, audit_row, canonical_index, audit_form, &legal_count, &first_legal_word);
        audit_join = audit_join && row_audited;
        if (legal_count != 0)
        {
            audit_rows_exercised += 1;
            if (baseline_word == UINT32_MAX)
            {
                baseline_word = first_legal_word;
            }
        }
        else
        {
            audit_all_exercised = false;
        }
        audit_legal_total += legal_count;
    }
    BUSTER_TEST(arguments, audit_join);
    BUSTER_TEST(arguments, audit_all_exercised && audit_rows_exercised == buster_a64_complex_simd_row_count());
    BUSTER_TEST(arguments, audit_legal_total >= audit_rows_exercised);
    BUSTER_TEST(arguments, audit_legal_total != 0u);

    /* Provenance preserves non-invertible field projections while still
     * rejecting typed mutations and tampered raw fields transactionally. */
    BusterA64ComplexSIMDResult provenance_decoded = {0};
    bool provenance_decode_ok = buster_a64_complex_simd_decode_row(target, 13, UINT32_C(0x0e010400), &provenance_decoded) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK;
    bool provenance_roundtrip_ok = false;
    bool provenance_mutation_rejected = false;
    bool provenance_tamper_rejected = false;
    if (provenance_decode_ok)
    {
        BusterA64ComplexSIMDInstruction provenance_instruction = {.row_index = 13, .operand_count = (u8)provenance_decoded.operand_count,
                                                                 .raw_fields_valid = provenance_decoded.raw_fields_valid,
                                                                 .raw_fields = provenance_decoded.raw_fields};
        for (u32 index = 0; index < provenance_decoded.operand_count; index += 1)
        {
            provenance_instruction.operands[index] = provenance_decoded.operands[index];
        }
        u32 provenance_word = UINT32_C(0xa5a5a5a5);
        provenance_roundtrip_ok = buster_a64_complex_simd_encode(target, &provenance_instruction, &provenance_word) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
                                  provenance_word == UINT32_C(0x0e010400);
        provenance_instruction.operands[0].payload ^= 1;
        provenance_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus mutation_status = buster_a64_complex_simd_encode(target, &provenance_instruction, &provenance_word);
        provenance_mutation_rejected = mutation_status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK && provenance_word == UINT32_C(0xa5a5a5a5);
        provenance_instruction.operands[0] = provenance_decoded.operands[0];
        provenance_instruction.raw_fields.values[0] ^= 1;
        provenance_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus tamper_status = buster_a64_complex_simd_encode(target, &provenance_instruction, &provenance_word);
        provenance_tamper_rejected = tamper_status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK && provenance_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, provenance_decode_ok);
    BUSTER_TEST(arguments, provenance_roundtrip_ok);
    BUSTER_TEST(arguments, provenance_mutation_rejected);
    BUSTER_TEST(arguments, provenance_tamper_rejected);

    /* By-element FMLA preserves the generated row's scalar lane-index
     * transform in both directions, including two independent LLVM seeds. */
    BusterA64ComplexSIMDRowInfo fmla_lane_row = {0};
    bool fmla_lane_row_ok = buster_a64_complex_simd_row(111, &fmla_lane_row) &&
        fmla_lane_row.semantic_form_id == 492 && fmla_lane_row.operand_count == 7;
    BUSTER_TEST(arguments, fmla_lane_row_ok);
    static u32 const fmla_lane_words[] = {UINT32_C(0x4f821020), UINT32_C(0x4f951280)};
    bool fmla_lane_roundtrips[BUSTER_ARRAY_LENGTH(fmla_lane_words)] = {0};
    BusterA64ComplexSIMDInstruction fmla_lane_seed_zero = {0};
    for (u32 seed_index = 0; seed_index < BUSTER_ARRAY_LENGTH(fmla_lane_words); seed_index += 1)
    {
        BusterA64ComplexSIMDResult decoded = {0};
        bool decoded_ok = fmla_lane_row_ok &&
            buster_a64_complex_simd_decode_row(target, 111, fmla_lane_words[seed_index], &decoded) ==
                BUSTER_A64_COMPLEX_SIMD_STATUS_OK;
        BusterA64ComplexSIMDInstruction instruction = {
            .row_index = 111,
            .operand_count = (u8)decoded.operand_count,
            .raw_fields_valid = decoded.raw_fields_valid,
            .raw_fields = decoded.raw_fields,
        };
        for (u32 operand_index = 0; decoded_ok && operand_index < decoded.operand_count; operand_index += 1)
        {
            instruction.operands[operand_index] = decoded.operands[operand_index];
        }
        u32 encoded = UINT32_C(0xa5a5a5a5);
        fmla_lane_roundtrips[seed_index] = decoded_ok &&
            buster_a64_complex_simd_encode(target, &instruction, &encoded) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
            encoded == fmla_lane_words[seed_index];
        if (seed_index == 0 && decoded_ok)
        {
            fmla_lane_seed_zero = instruction;
        }
    }
    BUSTER_TEST(arguments, fmla_lane_roundtrips[0] && fmla_lane_roundtrips[1]);

    bool fmla_lane_bad_index_rejected = false;
    if (fmla_lane_roundtrips[0])
    {
        BusterA64ComplexSIMDInstruction invalid_lane = fmla_lane_seed_zero;
        invalid_lane.raw_fields_valid = 0;
        invalid_lane.operands[6].payload = 4;
        u32 preserved_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus status =
            buster_a64_complex_simd_encode(target, &invalid_lane, &preserved_word);
        fmla_lane_bad_index_rejected = status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
            preserved_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, fmla_lane_bad_index_rejected);

    BusterA64ComplexSIMDRowInfo fmla_half_row = {0};
    BusterAarch64CanonicalFormInfo fmla_half_canonical = {0};
    u32 fmla_half_canonical_index = UINT32_MAX;
    bool fmla_half_row_found = buster_a64_complex_simd_row(110, &fmla_half_row) &&
        fmla_half_row.semantic_form_id == 491 &&
        a64_complex_simd_audit_canonical_form(fmla_half_row.source_digest, &fmla_half_canonical_index,
                                               &fmla_half_canonical);
    BusterA64ComplexSIMDResult fmla_half_decoded = {0};
    bool fmla_half_decoded_ok = fmla_half_row_found &&
        buster_a64_complex_simd_decode_row(target, 110, fmla_half_canonical.representative_word,
                                           &fmla_half_decoded) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK;
    bool fmla_half_register_range_rejected = false;
    if (fmla_half_decoded_ok && fmla_half_decoded.operand_count == 6)
    {
        BusterA64ComplexSIMDInstruction invalid_half_register = {
            .row_index = 110,
            .operand_count = (u8)fmla_half_decoded.operand_count,
        };
        for (u32 operand_index = 0; operand_index < fmla_half_decoded.operand_count; operand_index += 1)
        {
            invalid_half_register.operands[operand_index] = fmla_half_decoded.operands[operand_index];
        }
        invalid_half_register.raw_fields_valid = 0;
        invalid_half_register.operands[4].payload = 16;
        u32 preserved_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus status =
            buster_a64_complex_simd_encode(target, &invalid_half_register, &preserved_word);
        fmla_half_register_range_rejected = status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
            preserved_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, fmla_half_register_range_rejected);

    /* Fixed-H scalar rows must reject scalar S/D typed inputs even when their
     * register number and encoded word would otherwise match the H row. */
    BusterA64ComplexSIMDRowInfo fixed_h_row = {0};
    BusterAarch64CanonicalFormInfo fixed_h_canonical = {0};
    u32 fixed_h_canonical_index = UINT32_MAX;
    bool fixed_h_row_found = buster_a64_complex_simd_row(112, &fixed_h_row) &&
        fixed_h_row.semantic_form_id == 495 &&
        a64_complex_simd_audit_canonical_form(fixed_h_row.source_digest, &fixed_h_canonical_index,
                                               &fixed_h_canonical);
    u32 fixed_h_word = UINT32_MAX;
    u32 fixed_h_legal_count = 0;
    bool fixed_h_word_found = fixed_h_row_found &&
        a64_complex_simd_audit_row(target, 112, fixed_h_row, fixed_h_canonical_index, fixed_h_canonical,
                                   &fixed_h_legal_count, &fixed_h_word) &&
        fixed_h_legal_count != 0 && fixed_h_word != UINT32_MAX;
    BusterA64ComplexSIMDResult fixed_h_decoded = {0};
    bool fixed_h_decoded_ok = fixed_h_word_found &&
        buster_a64_complex_simd_decode_row(target, 112, fixed_h_word, &fixed_h_decoded) ==
            BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
        fixed_h_decoded.operand_count == 4;
    BusterA64ComplexSIMDInstruction fixed_h_typed = {
        .row_index = 112,
        .operand_count = (u8)fixed_h_decoded.operand_count,
    };
    for (u32 operand_index = 0; fixed_h_decoded_ok && operand_index < fixed_h_decoded.operand_count; operand_index += 1)
    {
        fixed_h_typed.operands[operand_index] = fixed_h_decoded.operands[operand_index];
    }
    u32 fixed_h_typed_word = UINT32_C(0xa5a5a5a5);
    bool fixed_h_typed_roundtrip = fixed_h_decoded_ok &&
        buster_a64_complex_simd_encode(target, &fixed_h_typed, &fixed_h_typed_word) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
        fixed_h_typed_word == fixed_h_word;
    BusterA64ComplexSIMDArrangement fixed_h_wrong_arrangements[] = {
        BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_S,
        BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_D,
    };
    bool fixed_h_wrong_arrangements_rejected[BUSTER_ARRAY_LENGTH(fixed_h_wrong_arrangements)] = {0};
    for (u32 arrangement_index = 0; fixed_h_decoded_ok && arrangement_index < BUSTER_ARRAY_LENGTH(fixed_h_wrong_arrangements);
         arrangement_index += 1)
    {
        BusterA64ComplexSIMDInstruction invalid_fixed_h = fixed_h_typed;
        invalid_fixed_h.operands[0] = buster_a64_complex_simd_value_scalar(
            (u32)fixed_h_decoded.operands[0].payload, fixed_h_wrong_arrangements[arrangement_index]);
        u32 preserved_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus status =
            buster_a64_complex_simd_encode(target, &invalid_fixed_h, &preserved_word);
        fixed_h_wrong_arrangements_rejected[arrangement_index] =
            status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK && preserved_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, fixed_h_typed_roundtrip);
    BUSTER_TEST(arguments, fixed_h_wrong_arrangements_rejected[0] && fixed_h_wrong_arrangements_rejected[1]);

    bool fmla_lane_shape_conflict_rejected = false;
    if (fmla_lane_roundtrips[0])
    {
        BusterA64ComplexSIMDInstruction invalid_shape = fmla_lane_seed_zero;
        invalid_shape.raw_fields_valid = 0;
        invalid_shape.operands[5] =
            buster_a64_complex_simd_value_arrangement(BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_D);
        u32 preserved_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus status =
            buster_a64_complex_simd_encode(target, &invalid_shape, &preserved_word);
        fmla_lane_shape_conflict_rejected = status != BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
            preserved_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, fmla_lane_shape_conflict_rejected);

    /* ROTATE is a distinct typed VM value, even though its operand
     * transform ultimately encodes a numeric table entry. Row 35's audit
     * representative is selector-reserved (0x2f001000), so seed from its
     * legal S-element encoding and retain an explicit rejection check. */
    BusterA64ComplexSIMDRowInfo fcmla_lane_row = {0};
    BusterAarch64CanonicalFormInfo fcmla_lane_canonical = {0};
    u32 fcmla_lane_canonical_index = UINT32_MAX;
    bool fcmla_lane_row_ok = buster_a64_complex_simd_row(35, &fcmla_lane_row) &&
        fcmla_lane_row.semantic_form_id == 283 && fcmla_lane_row.operand_count == 8 &&
        a64_complex_simd_audit_canonical_form(fcmla_lane_row.source_digest, &fcmla_lane_canonical_index,
                                               &fcmla_lane_canonical);
    u32 const fcmla_lane_seed_word = UINT32_C(0x2f401000);
    BusterA64ComplexSIMDResult fcmla_lane_decoded = {0};
    bool fcmla_lane_decoded_ok = fcmla_lane_row_ok &&
        buster_a64_complex_simd_decode_row(target, 35, fcmla_lane_seed_word,
                                           &fcmla_lane_decoded) == BUSTER_A64_COMPLEX_SIMD_STATUS_OK;
    BusterA64ComplexSIMDResult fcmla_reserved_selector = {0};
    bool fcmla_reserved_selector_rejected = fcmla_lane_row_ok &&
        buster_a64_complex_simd_decode_row(target, 35, UINT32_C(0x2f001000),
                                           &fcmla_reserved_selector) == BUSTER_A64_COMPLEX_SIMD_STATUS_RESERVED;
    BusterA64ComplexSIMDInstruction fcmla_lane_instruction = {.row_index = 35, .operand_count = 8};
    for (u32 operand_index = 0; fcmla_lane_decoded_ok && operand_index < fcmla_lane_decoded.operand_count; operand_index += 1)
    {
        fcmla_lane_instruction.operands[operand_index] = fcmla_lane_decoded.operands[operand_index];
    }
    fcmla_lane_instruction.operands[7] = (BusterA64SemanticVMValue){
        .kind = BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE, .width = 8, .payload = 90};
    u32 fcmla_lane_word = UINT32_C(0xa5a5a5a5);
    BusterA64ComplexSIMDStatus fcmla_lane_encode_status =
        buster_a64_complex_simd_encode(target, &fcmla_lane_instruction, &fcmla_lane_word);
    BusterA64ComplexSIMDResult fcmla_lane_roundtrip = {0};
    BusterA64ComplexSIMDStatus fcmla_lane_decode_status =
        buster_a64_complex_simd_decode_row(target, 35, fcmla_lane_word, &fcmla_lane_roundtrip);
    bool fcmla_lane_typed_operands_equal = fcmla_lane_decoded_ok &&
        fcmla_lane_decode_status == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
        fcmla_lane_roundtrip.operand_count == fcmla_lane_instruction.operand_count;
    for (u32 operand_index = 0; fcmla_lane_typed_operands_equal && operand_index < fcmla_lane_roundtrip.operand_count; operand_index += 1)
    {
        BusterA64SemanticVMValue expected = fcmla_lane_instruction.operands[operand_index];
        BusterA64SemanticVMValue actual = fcmla_lane_roundtrip.operands[operand_index];
        bool text_equal = expected.text.length == actual.text.length;
        for (u32 byte_index = 0; text_equal && byte_index < expected.text.length; byte_index += 1)
        {
            text_equal = buster_a64_semantic_string_byte(expected.text, byte_index) ==
                buster_a64_semantic_string_byte(actual.text, byte_index);
        }
        fcmla_lane_typed_operands_equal = expected.kind == actual.kind && expected.width == actual.width &&
            expected.flags == actual.flags && expected.aux == actual.aux && expected.aux2 == actual.aux2 &&
            expected.payload == actual.payload && expected.mask == actual.mask && text_equal;
    }
    BUSTER_TEST(arguments, fcmla_lane_row_ok && fcmla_lane_decoded_ok &&
        fcmla_reserved_selector_rejected &&
        fcmla_lane_encode_status == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
        fcmla_lane_decode_status == BUSTER_A64_COMPLEX_SIMD_STATUS_OK &&
        fcmla_lane_typed_operands_equal &&
        fcmla_lane_roundtrip.operands[7].kind == BUSTER_A64_SEMANTIC_VM_VALUE_ROTATE &&
        fcmla_lane_roundtrip.operands[7].width == 8 && fcmla_lane_roundtrip.operands[7].payload == 90);

    /* Feature filtering and failed-output transactionality are checked on a
     * known legal baseline encoding. */
    Target no_features = target;
    no_features.cpu_features_explicit = true;
    no_features.cpu_features = target_cpu_features_empty();
    bool fmla_lane_feature_rejected = false;
    if (fmla_lane_roundtrips[0])
    {
        u32 preserved_word = UINT32_C(0xa5a5a5a5);
        BusterA64ComplexSIMDStatus status =
            buster_a64_complex_simd_encode(no_features, &fmla_lane_seed_zero, &preserved_word);
        fmla_lane_feature_rejected = status == BUSTER_A64_COMPLEX_SIMD_STATUS_TARGET_MISMATCH &&
            preserved_word == UINT32_C(0xa5a5a5a5);
    }
    BUSTER_TEST(arguments, fmla_lane_feature_rejected);
    BusterA64ComplexSIMDResult preserved = {
        .status = BUSTER_A64_COMPLEX_SIMD_STATUS_AMBIGUOUS, .row_index = UINT32_C(0x12345678), .word = UINT32_C(0x89abcdef), .operand_count = 7};
    preserved.operands[0].payload = UINT64_C(0xfeedface);
    BusterA64ComplexSIMDResult saved = preserved;
    BUSTER_TEST(arguments, baseline_word != UINT32_MAX);
    BUSTER_TEST(arguments, buster_a64_complex_simd_decode_row(no_features, 0, baseline_word, &preserved) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, preserved.status == saved.status && preserved.row_index == saved.row_index && preserved.word == saved.word &&
                               preserved.operand_count == saved.operand_count && preserved.operands[0].payload == saved.operands[0].payload);
    preserved = saved;
    BUSTER_TEST(arguments, buster_a64_complex_simd_decode(no_features, UINT32_C(0xffffffff), &preserved) != BUSTER_A64_COMPLEX_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, preserved.status == saved.status && preserved.row_index == saved.row_index && preserved.word == saved.word &&
                               preserved.operand_count == saved.operand_count && preserved.operands[0].payload == saved.operands[0].payload);

    return result;
}

#endif
