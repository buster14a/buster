#include <buster/tests/compiler/assembly/aarch64_direct_simd_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>

static Target a64_direct_simd_test_target(void)
{
    return (Target){.cpu_arch = CPU_ARCH_AARCH64,
                    .cpu_model = CPU_MODEL_A64_APPLE_M1,
                    .os = OPERATING_SYSTEM_MACOS,
                    .cpu_features_explicit = true,
                    .cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1)};
}

static u32 a64_direct_simd_audit_mix(u32 value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static bool a64_direct_simd_audit_canonical_form(u64 digest, u32* form_index, BusterAarch64CanonicalFormInfo* result)
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

static bool a64_direct_simd_audit_word(Target target, u32 row_index, u64 digest, u32 canonical_index, u32 word, u32* first_word)
{
    BusterAarch64CanonicalDecodeResult canonical = {0};
    if (buster_aarch64_canonical_decode(target, word, &canonical) != BUSTER_AARCH64_CANONICAL_DECODE_SUCCESS || canonical.form_index != canonical_index ||
        canonical.arm_row_digest != digest)
    {
        return false;
    }
    BusterA64DirectSIMDResult typed = {0};
    if (buster_a64_direct_simd_decode_row(target, row_index, word, &typed) != BUSTER_A64_DIRECT_SIMD_STATUS_OK)
    {
        return false;
    }
    BusterA64DirectSIMDInstruction instruction = {.row_index = row_index, .operand_count = (u8)typed.operand_count};
    for (u32 index = 0; index < typed.operand_count; index += 1)
    {
        instruction.operands[index] = typed.operands[index];
    }
    u32 reencoded = 0;
    if (buster_a64_direct_simd_encode(target, &instruction, &reencoded) != BUSTER_A64_DIRECT_SIMD_STATUS_OK || reencoded != word)
    {
        return false;
    }
    if (first_word && *first_word == UINT32_MAX)
    {
        *first_word = word;
    }
    return true;
}

static bool a64_direct_simd_audit_try_word(Target target, u32 row_index, u64 digest, u32 canonical_index, u32 word, u32* tried, u32* tried_count,
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
    if (a64_direct_simd_audit_word(target, row_index, digest, canonical_index, word, first_word))
    {
        *legal_count += 1;
    }
    return true;
}

static bool a64_direct_simd_audit_row(Target target, u32 row_index, BusterA64DirectSIMDRowInfo row, u32 canonical_index,
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
    a64_direct_simd_audit_try_word(target, row_index, row.source_digest, canonical_index, canonical.representative_word, tried, &tried_count, legal_count,
                                   first_word);
    for (u32 field_index = 0; field_index < canonical.field_count; field_index += 1)
    {
        BusterAarch64CanonicalFieldInfo field = {0};
        if (!buster_aarch64_canonical_field(canonical_index, field_index, &field) || field.width == 0)
        {
            return false;
        }
        u32 maximum = field.source_mask;
        u32 samples[12] = {0};
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
                samples[sample_count++] = a64_direct_simd_audit_mix(row_index * 131u + field_index * 17u + random_index) & maximum;
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
                a64_direct_simd_audit_try_word(target, row_index, row.source_digest, canonical_index, word, tried, &tried_count, legal_count, first_word);
            }
        }
    }
    return true;
}

UnitTestResult aarch64_direct_simd_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Target target = a64_direct_simd_test_target();
    BUSTER_TEST(arguments, buster_a64_direct_simd_schema_version() == 1);
    BUSTER_TEST(arguments, buster_a64_direct_simd_row_count() == 390);
    BUSTER_TEST(arguments, buster_a64_direct_simd_transform_row_count() == 263);
    BUSTER_TEST(arguments, buster_a64_direct_simd_max_operands() == 8);
    BUSTER_TEST(arguments, buster_a64_direct_simd_validate());
    BusterA64DirectSIMDRowInfo row = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_row(0, &row));
    BUSTER_TEST(arguments, row.semantic_form_id == 0 && row.operand_count == 4);

    BusterA64DirectSIMDInstruction instruction = {.row_index = 0, .operand_count = 4};
    instruction.operands[0] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
    instruction.operands[1] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
    instruction.operands[2] = buster_a64_direct_simd_value_vector(2, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
    instruction.operands[3] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
    u32 word = 0;
    BusterA64DirectSIMDStatus status = buster_a64_direct_simd_encode(target, &instruction, &word);
    BUSTER_TEST(arguments, status == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BusterA64DirectSIMDResult decoded = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(target, 0, word, &decoded) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, decoded.row_index == 0 && decoded.word == word && decoded.operand_count == 4);
    BUSTER_TEST(arguments, decoded.operands[0].payload == 1 && decoded.operands[2].payload == 2);
    BUSTER_TEST(arguments, decoded.operands[1].aux == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);

    u32 before = UINT32_C(0xdeadbeef);
    instruction.operands[1] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_INVALID);
    BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &instruction, &before) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, before == UINT32_C(0xdeadbeef));
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_list(31, 5, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_lane(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_4S, 4).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BusterA64SemanticVMValue vector64 = buster_a64_direct_simd_value_vector(31, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
    BusterA64SemanticVMValue vector128 = buster_a64_direct_simd_value_vector(31, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
    BUSTER_TEST(arguments, vector64.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && vector64.width == 64);
    BUSTER_TEST(arguments, vector128.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && vector128.width == 128);
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_vector(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_scalar(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_scalar(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_S).width == 32);
    BUSTER_TEST(arguments, buster_a64_direct_simd_value_list(31, 4, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B).width == 64);
    BusterA64SemanticVMValue lane7 = buster_a64_direct_simd_value_lane(31, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B, 7);
    BUSTER_TEST(arguments, lane7.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE && lane7.aux2 == 7);

    /* A width selector such as ADDV's <V> selects a scalar register
     * arrangement (B/H/S), while its source uses a numbered vector. */
    BusterA64DirectSIMDInstruction addv = {.row_index = 4, .operand_count = 4};
    addv.operands[0] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B);
    addv.operands[1] = buster_a64_direct_simd_value_scalar(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B);
    addv.operands[2] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
    addv.operands[3] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
    u32 addv_word = 0;
    BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &addv, &addv_word) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BusterA64DirectSIMDResult addv_decoded = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(target, 4, addv_word, &addv_decoded) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, addv_decoded.operands[1].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR &&
                               addv_decoded.operands[1].aux == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_B);
    BUSTER_TEST(arguments, addv_decoded.operands[2].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR &&
                               addv_decoded.operands[2].aux == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);

    BusterA64DirectSIMDInstruction addp = {.row_index = 3, .operand_count = 2};
    addp.operands[0] = buster_a64_direct_simd_value_scalar(0, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D);
    addp.operands[1] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D);
    u32 addp_word = 0;
    BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &addp, &addp_word) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BusterA64DirectSIMDResult addp_decoded = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(target, 3, addp_word, &addp_decoded) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, addp_decoded.operands[0].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_SCALAR &&
                               addp_decoded.operands[0].aux == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_D);
    BUSTER_TEST(arguments, addp_decoded.operands[1].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR &&
                               addp_decoded.operands[1].aux == BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_2D);

    bool audit_join = true;
    bool audit_all_exercised = true;
    u32 audit_rows_exercised = 0;
    u32 audit_legal_total = 0;
    for (u32 audit_index = 0; audit_index < buster_a64_direct_simd_row_count(); audit_index += 1)
    {
        BusterA64DirectSIMDRowInfo audit_row = {0};
        BusterAarch64CanonicalFormInfo audit_form = {0};
        BusterA64SemanticForm audit_semantic_form = {0};
        u32 canonical_index = UINT32_MAX;
        bool row_found = buster_a64_direct_simd_row(audit_index, &audit_row) &&
                         a64_direct_simd_audit_canonical_form(audit_row.source_digest, &canonical_index, &audit_form) &&
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
        bool row_audited = row_found && a64_direct_simd_audit_row(target, audit_index, audit_row, canonical_index, audit_form, &legal_count, &first_legal_word);
        audit_join = audit_join && row_audited;
        if (legal_count != 0)
        {
            audit_rows_exercised += 1;
        }
        else
        {
            audit_all_exercised = false;
        }
        audit_legal_total += legal_count;
    }
    BUSTER_TEST(arguments, audit_join);
    BUSTER_TEST(arguments, audit_all_exercised && audit_rows_exercised == buster_a64_direct_simd_row_count());
    BUSTER_TEST(arguments, audit_legal_total >= audit_rows_exercised);
    BUSTER_TEST(arguments, audit_legal_total == 5962u);

    /* Feature filtering and failed-output transactionality are checked on a
     * known legal baseline encoding. */
    Target no_features = target;
    no_features.cpu_features_explicit = true;
    no_features.cpu_features = target_cpu_features_empty();
    BusterA64DirectSIMDResult preserved = {
        .status = BUSTER_A64_DIRECT_SIMD_STATUS_AMBIGUOUS, .row_index = UINT32_C(0x12345678), .word = UINT32_C(0x89abcdef), .operand_count = 7};
    preserved.operands[0].payload = UINT64_C(0xfeedface);
    BusterA64DirectSIMDResult saved = preserved;
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(no_features, 0, word, &preserved) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, preserved.status == saved.status && preserved.row_index == saved.row_index && preserved.word == saved.word &&
                               preserved.operand_count == saved.operand_count && preserved.operands[0].payload == saved.operands[0].payload);
    preserved = saved;
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode(no_features, UINT32_C(0xffffffff), &preserved) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, preserved.status == saved.status && preserved.row_index == saved.row_index && preserved.word == saved.word &&
                               preserved.operand_count == saved.operand_count && preserved.operands[0].payload == saved.operands[0].payload);

    BusterA64DirectSIMDInstruction dup = {.row_index = 32, .operand_count = 4};
    dup.operands[0] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
    dup.operands[1] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
    dup.operands[2] = buster_a64_direct_simd_value_gpr_width(32);
    dup.operands[3] = buster_a64_direct_simd_value_gpr(2, 32, false);
    u32 dup_word = 0;
    BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &dup, &dup_word) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BusterA64DirectSIMDResult dup_decoded = {0};
    BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(target, 32, dup_word, &dup_decoded) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, dup_decoded.operands[2].kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION);
    BUSTER_TEST(arguments, dup_decoded.operands[3].kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && dup_decoded.operands[3].width == 32);
    dup.operands[2] = buster_a64_direct_simd_value_gpr_width(64);
    before = UINT32_C(0x13579bdf);
    BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &dup, &before) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
    BUSTER_TEST(arguments, before == UINT32_C(0x13579bdf));

    /* TBL/TBX's eight metadata-labeled SIMD_LANE operands are actually index
     * vectors: Vm.<Ta> has no encoded element-lane field.  Exercise L1-L4 for
     * both mnemonics with wraparound list bases and exact decode/re-encode. */
    for (u32 table_index = 0; table_index < 8; table_index += 1)
    {
        u32 row_index = 340u + table_index;
        u32 list_count = (table_index & 3u) + 1u;
        u32 operand_count = list_count + 4u;
        BusterA64DirectSIMDInstruction table = {.row_index = row_index, .operand_count = (u8)operand_count};
        table.operands[0] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        table.operands[1] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        for (u32 list_index = 0; list_index < list_count; list_index += 1)
        {
            table.operands[2 + list_index] = buster_a64_direct_simd_value_list(31, list_count, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        }
        table.operands[2 + list_count] = buster_a64_direct_simd_value_vector(2, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        table.operands[3 + list_count] = buster_a64_direct_simd_value_arrangement(BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        u32 table_word = 0;
        BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &table, &table_word) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
        BusterA64DirectSIMDResult table_decoded = {0};
        BUSTER_TEST(arguments, buster_a64_direct_simd_decode_row(target, row_index, table_word, &table_decoded) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);
        BUSTER_TEST(arguments, table_decoded.operands[0].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR && table_decoded.operands[0].width == 64);
        BUSTER_TEST(arguments, table_decoded.operands[2].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && table_decoded.operands[2].payload == 31 &&
                                   table_decoded.operands[2].aux2 == list_count);
        BUSTER_TEST(arguments, table_decoded.operands[2 + list_count].kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_VECTOR);
        BUSTER_TEST(arguments, table_decoded.operands[2 + list_count].payload == 2);
        BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &table, &table_word) == BUSTER_A64_DIRECT_SIMD_STATUS_OK);

        BusterA64DirectSIMDInstruction wrong_kind = table;
        wrong_kind.operands[2] = buster_a64_direct_simd_value_vector(31, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B);
        before = UINT32_C(0x2468ace0);
        BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &wrong_kind, &before) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
        BUSTER_TEST(arguments, before == UINT32_C(0x2468ace0));
        wrong_kind = table;
        wrong_kind.operands[2 + list_count] = buster_a64_direct_simd_value_lane(2, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_8B, 0);
        before = UINT32_C(0x2468ace1);
        BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &wrong_kind, &before) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
        BUSTER_TEST(arguments, before == UINT32_C(0x2468ace1));
        wrong_kind = table;
        wrong_kind.operands[0] = buster_a64_direct_simd_value_vector(1, BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_16B);
        before = UINT32_C(0x2468ace2);
        BUSTER_TEST(arguments, buster_a64_direct_simd_encode(target, &wrong_kind, &before) != BUSTER_A64_DIRECT_SIMD_STATUS_OK);
        BUSTER_TEST(arguments, before == UINT32_C(0x2468ace2));
    }
    return result;
}

#endif
