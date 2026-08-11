#include <buster/tests/compiler/assembly/aarch64_semantics_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_semantics.h>

static bool a64_semantic_string_is(BusterA64SemanticString actual, String8 expected)
{
    if (actual.length != expected.length) return false;
    for (u32 index = 0; index < actual.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(actual, index) != expected.pointer[index]) return false;
    }
    return true;
}

static bool a64_semantic_value_text_is(u32 atom_id, String8 expected)
{
    BusterA64SemanticValueAtom atom;
    return buster_a64_semantic_value_atom(atom_id, &atom) && a64_semantic_string_is(atom.text, expected);
}

static bool a64_semantic_atom_equal(u32 left_id, u32 right_id)
{
    BusterA64SemanticValueAtom left = {0};
    BusterA64SemanticValueAtom right = {0};
    if (!buster_a64_semantic_value_atom(left_id, &left) || !buster_a64_semantic_value_atom(right_id, &right) ||
        left.kind != right.kind || left.integer != right.integer || left.text.length != right.text.length) return false;
    for (u32 index = 0; index < left.text.length; index += 1)
    {
        if (buster_a64_semantic_string_byte(left.text, index) != buster_a64_semantic_string_byte(right.text, index)) return false;
    }
    return true;
}

static bool a64_semantic_value_equal(BusterA64SemanticValue left, BusterA64SemanticValue right)
{
    if (left.key_count != right.key_count || left.result_count != right.result_count) return false;
    for (u32 index = 0; index < left.key_count; index += 1)
    {
        if (!a64_semantic_atom_equal(left.key_first + index, right.key_first + index)) return false;
    }
    for (u32 index = 0; index < left.result_count; index += 1)
    {
        if (!a64_semantic_atom_equal(left.result_first + index, right.result_first + index)) return false;
    }
    return true;
}

UnitTestResult aarch64_semantics_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, buster_a64_semantic_validate());
    BUSTER_TEST(arguments, buster_a64_semantic_form_count() == 1695);
    BUSTER_TEST(arguments, buster_a64_semantic_operand_field_index_count() >= buster_a64_semantic_operand_count());

    /* ABS's arrangement table is the binding canary: both 8B and 16B must
       survive normalization, and a checked value accessor must expose the
       same key/result shape for every direction. */
    u32 form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:ABS_asimdmisc_R"), 0, &form_id));
    BusterA64SemanticForm form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(form_id, &form) && form.transform_count >= 2);
    BusterA64SemanticTransform table = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_transform(form.transform_first + 1, &table) && table.kind == BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE && table.value_count == 8);
    if (table.value_count == 8)
    {
        BusterA64SemanticValue first = {0};
        BusterA64SemanticValue second = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, 0, &first) && first.key_count == 2 && first.result_count == 1);
        BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, 1, &second) && second.key_count == 2 && second.result_count == 1);
        BusterA64SemanticValueAtom first_result = {0};
        BusterA64SemanticValueAtom second_result = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_value_atom(first.result_first, &first_result) && first_result.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM);
        BUSTER_TEST(arguments, buster_a64_semantic_value_atom(second.result_first, &second_result) && second_result.kind == BUSTER_A64_SEMANTIC_VALUE_ENUM);
        BUSTER_TEST(arguments, a64_semantic_value_text_is(first.result_first, S8("8B")));
        BUSTER_TEST(arguments, a64_semantic_value_text_is(second.result_first, S8("16B")));

        /* Decode -> encode canary: each unique result points back to exactly
           one key in the invertible ABS table. */
        for (u32 value_index = 0; value_index < table.value_count; value_index += 1)
        {
            BusterA64SemanticValue value = {0};
            BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, value_index, &value) && value.result_count == 1);
            for (u32 inverse_index = 0; inverse_index < table.value_count; inverse_index += 1)
            {
                BusterA64SemanticValue inverse = {0};
                BUSTER_TEST(arguments, buster_a64_semantic_transform_value(table.id, inverse_index, &inverse));
                if (a64_semantic_atom_equal(value.result_first, inverse.result_first))
                {
                    BUSTER_TEST(arguments, a64_semantic_value_equal(value, inverse));
                }
            }
        }
    }

    /* Keep non-injective value tables explicit: a result collision is valid
       for presentation metadata, but it must not be presented as invertible. */
    bool saw_non_injective_table = false;
    for (u32 transform_id = 0; transform_id < buster_a64_semantic_transform_count() && !saw_non_injective_table; transform_id += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(transform_id, &transform) || transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE || transform.invertible) continue;
        for (u32 left_index = 0; left_index < transform.value_count && !saw_non_injective_table; left_index += 1)
        {
            BusterA64SemanticValue left = {0};
            if (!buster_a64_semantic_transform_value(transform_id, left_index, &left)) continue;
            for (u32 right_index = left_index + 1; right_index < transform.value_count; right_index += 1)
            {
                BusterA64SemanticValue right = {0};
                if (buster_a64_semantic_transform_value(transform_id, right_index, &right) &&
                    a64_semantic_atom_equal(left.result_first, right.result_first))
                {
                    saw_non_injective_table = true;
                    break;
                }
            }
        }
    }
    BUSTER_TEST(arguments, saw_non_injective_table);

    /* Classification is deliberately presentation-only.  In particular the
       literal ``simm`` XML link is an integer memory offset, not a SIMD
       operand, and a store row must not claim a write direction without
       instruction-level data-flow semantics. */
    u32 store_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:STR_32_ldst_immpost"), 0, &store_id));
    BusterA64SemanticForm store = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(store_id, &store));
    bool saw_simm = false;
    u32 simd_kind_mask = (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_REGISTER) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_ARRANGEMENT) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_WIDTH_SELECTOR) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_LIST) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_LANE) |
                         (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_SIMD_PREFIX_SELECTOR);
    for (u32 operand_index = 0; operand_index < store.operand_count; operand_index += 1)
    {
        BusterA64SemanticOperand operand = {0};
        BUSTER_TEST(arguments, buster_a64_semantic_operand(store.operand_first + operand_index, &operand));
        BUSTER_TEST(arguments, operand.classification_status == BUSTER_A64_SEMANTIC_CLASSIFICATION_PRESENTATION_ONLY);
        BUSTER_TEST(arguments, a64_semantic_string_is(operand.direction, S8("unknown")));
        if (a64_semantic_string_is(operand.link, S8("simm__3")))
        {
            saw_simm = true;
            BUSTER_TEST(arguments, operand.kind == BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE);
            BUSTER_TEST(arguments, (operand.kind_mask & (UINT32_C(1) << BUSTER_A64_SEMANTIC_OPERAND_INTEGER_IMMEDIATE)) != 0);
            BUSTER_TEST(arguments, (operand.kind_mask & simd_kind_mask) == 0);
        }
    }
    BUSTER_TEST(arguments, saw_simm);

    /* Checked accessors reject bad IDs/ranges without clobbering caller
       storage. */
    BusterA64SemanticForm invalid_form = {.id = 0x12345678u, .name = {1, 2}, .mnemonic = {3, 4}, .assembly = {5, 6}, .source_digest = 7,
                                          .fixed_mask = 8, .fixed_value = 9, .field_first = 10, .operand_first = 11, .transform_first = 12,
                                          .field_count = 13, .operand_count = 14, .transform_count = 15, .owner = 16, .kind = 17,
                                          .raw_layout_resolved = 18, .status = 19};
    BusterA64SemanticForm invalid_form_before = invalid_form;
    BUSTER_TEST(arguments, !buster_a64_semantic_form(UINT32_MAX, &invalid_form) && memcmp(&invalid_form, &invalid_form_before, sizeof(invalid_form)) == 0);
    BusterA64SemanticTransform invalid_transform = {.id = 0x22334455u, .expression = {7, 8}, .source = 9, .p0 = 10, .p1 = 11,
                                                    .part_first = 12, .value_first = 13, .part_count = 14, .value_count = 15, .kind = 16,
                                                    .invertible = true, .reserved = 17};
    BusterA64SemanticTransform invalid_transform_before = invalid_transform;
    BUSTER_TEST(arguments, !buster_a64_semantic_transform(UINT32_MAX, &invalid_transform) && memcmp(&invalid_transform, &invalid_transform_before, sizeof(invalid_transform)) == 0);
    u32 invalid_field_id = 0xabcdef01u;
    BUSTER_TEST(arguments, !buster_a64_semantic_operand_field_index(UINT32_MAX, 0, &invalid_field_id) && invalid_field_id == 0xabcdef01u);
    BusterA64SemanticValueAtom invalid_atom = {.id = 1, .kind = 2, .integer = 3, .text = {4, 5}};
    BusterA64SemanticValueAtom invalid_atom_before = invalid_atom;
    BUSTER_TEST(arguments, !buster_a64_semantic_value_atom(UINT32_MAX, &invalid_atom) && memcmp(&invalid_atom, &invalid_atom_before, sizeof(invalid_atom)) == 0);
    return result;
}

#endif
