#include <buster/tests/compiler/assembly/aarch64_semantic_vm_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>
#include <buster/lib/compiler/assembly/generated/aarch64-semantic-vm.generated.h>

#if defined(__has_include)
#if __has_include(<buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h>)
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#define BUSTER_A64_SEMANTIC_VM_TEST_HAS_CANONICAL_RAW 1
#endif
#endif
#ifndef BUSTER_A64_SEMANTIC_VM_TEST_HAS_CANONICAL_RAW
#define BUSTER_A64_SEMANTIC_VM_TEST_HAS_CANONICAL_RAW 0
#endif

static bool a64_vm_string_is(BusterA64SemanticString actual, String8 expected)
{
    if (actual.length != expected.length) return false;
    for (u32 index = 0; index < actual.length; index += 1)
        if (buster_a64_semantic_string_byte(actual, index) != expected.pointer[index]) return false;
    return true;
}

static bool a64_vm_find_transform(u32 form_id, u8 kind, u32* transform_id)
{
    BusterA64SemanticForm form = {0};
    if (!transform_id || !buster_a64_semantic_form(form_id, &form)) return false;
    for (u32 ordinal = 0; ordinal < form.transform_count; ordinal += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (buster_a64_semantic_transform(form.transform_first + ordinal, &transform) && transform.kind == kind)
        {
            *transform_id = transform.id;
            return true;
        }
    }
    return false;
}

static bool a64_vm_find_table_by_headers(u32 form_id, String8 first_name, String8 second_name, u32* transform_id)
{
    BusterA64SemanticForm form = {0};
    if (!transform_id || !buster_a64_semantic_form(form_id, &form)) return false;
    for (u32 ordinal = 0; ordinal < form.transform_count; ordinal += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(form.transform_first + ordinal, &transform) ||
            transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE) continue;
        u32 table_id = UINT32_MAX;
        BusterA64SemanticTableHeader table = {0};
        BusterA64SemanticString first = {0};
        BusterA64SemanticString second = {0};
        if (!buster_a64_semantic_transform_table_header(transform.id, &table_id) ||
            !buster_a64_semantic_table_header(table_id, &table) || table.key_header_count != 2 ||
            !buster_a64_semantic_table_key_header(table_id, 0, &first) ||
            !buster_a64_semantic_table_key_header(table_id, 1, &second)) continue;
        if (a64_vm_string_is(first, first_name) && a64_vm_string_is(second, second_name))
        {
            *transform_id = transform.id;
            return true;
        }
    }
    return false;
}

static bool a64_vm_find_table_by_header(u32 form_id, String8 name, u32* transform_id)
{
    BusterA64SemanticForm form = {0};
    if (!transform_id || !buster_a64_semantic_form(form_id, &form)) return false;
    for (u32 ordinal = 0; ordinal < form.transform_count; ordinal += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(form.transform_first + ordinal, &transform) ||
            transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE) continue;
        u32 table_id = UINT32_MAX;
        BusterA64SemanticTableHeader table = {0};
        BusterA64SemanticString header = {0};
        if (!buster_a64_semantic_transform_table_header(transform.id, &table_id) ||
            !buster_a64_semantic_table_header(table_id, &table) || table.key_header_count != 1 ||
            !buster_a64_semantic_table_key_header(table_id, 0, &header)) continue;
        if (a64_vm_string_is(header, name))
        {
            *transform_id = transform.id;
            return true;
        }
    }
    return false;
}

static bool a64_vm_find_program_table_transform(u32 form_id, u32* transform_id)
{
    BusterA64SemanticForm form = {0};
    if (!transform_id || !buster_a64_semantic_form(form_id, &form)) return false;
    for (u32 ordinal = 0; ordinal < form.transform_count; ordinal += 1)
    {
        BusterA64SemanticTransform transform = {0};
        if (!buster_a64_semantic_transform(form.transform_first + ordinal, &transform) || transform.kind != BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE)
            continue;
        for (u32 value_ordinal = 0; value_ordinal < transform.value_count; value_ordinal += 1)
        {
            BusterA64SemanticValue value = {0};
            if (!buster_a64_semantic_transform_value(transform.id, value_ordinal, &value)) return false;
            for (u32 result_ordinal = 0; result_ordinal < value.result_count; result_ordinal += 1)
            {
                BusterA64SemanticValueAtom atom = {0};
                if (!buster_a64_semantic_value_atom(value.result_first + result_ordinal, &atom)) return false;
                if (atom.kind == BUSTER_A64_SEMANTIC_VALUE_PROGRAM)
                {
                    *transform_id = transform.id;
                    return true;
                }
            }
        }
    }
    return false;
}

static bool a64_vm_set_field(u32 form_id, BusterA64SemanticVMFields* fields, String8 name, u32 value)
{
    BusterA64SemanticForm form = {0};
    if (!fields || !buster_a64_semantic_form(form_id, &form) || fields->count != form.field_count) return false;
    for (u32 ordinal = 0; ordinal < form.field_count; ordinal += 1)
    {
        BusterA64SemanticField field = {0};
        if (!buster_a64_semantic_field(form.field_first + ordinal, &field)) return false;
        if (a64_vm_string_is(field.name, name))
        {
            fields->values[ordinal] = value;
            return true;
        }
    }
    return false;
}

static bool a64_vm_raw_round_trip_all_rows(void)
{
#if BUSTER_A64_SEMANTIC_VM_TEST_HAS_CANONICAL_RAW
    u32 mapped = 0;
    for (u32 semantic_id = 0; semantic_id < BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT; semantic_id += 1)
    {
        BusterA64SemanticForm semantic = {0};
        if (!buster_a64_semantic_form(semantic_id, &semantic)) return false;
        u16 canonical_id = buster_a64_semantic_vm_canonical_raw_indices[semantic_id];
        if (canonical_id == UINT16_MAX) continue;
        mapped += 1;
        BusterAarch64CanonicalFormInfo canonical = {0};
        if (!buster_aarch64_canonical_form(canonical_id, &canonical) || canonical.field_count != semantic.field_count) return false;
        u32 canonical_fields[32] = {0};
        if (!buster_aarch64_canonical_raw_decode(canonical_id, canonical.representative_word, canonical_fields, canonical.field_count)) return false;
        BusterA64SemanticVMResult decoded = {0};
        if (buster_a64_semantic_vm_decode_fields(semantic_id, canonical.representative_word, &decoded) != BUSTER_A64_SEMANTIC_VM_STATUS_OK ||
            decoded.field_count != canonical.field_count) return false;
        for (u32 field = 0; field < canonical.field_count; field += 1)
        {
            if (decoded.fields.values[field] != canonical_fields[field]) return false;
        }
        u32 encoded = 0;
        if (buster_a64_semantic_vm_encode_fields(semantic_id, &decoded.fields, &encoded) != BUSTER_A64_SEMANTIC_VM_STATUS_OK ||
            encoded != canonical.representative_word) return false;
    }
    return mapped == BUSTER_AARCH64_SEMANTIC_VM_RAW_CODEC_COUNT;
#else
    return true;
#endif
}

UnitTestResult aarch64_semantic_vm_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_schema_version() == 2);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_form_count() == 1695);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_raw_codec_count() == 1522);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_transform_count() == 2808);
    BUSTER_TEST(arguments, buster_a64_semantic_parsed_program_count() == 372);
    BUSTER_TEST(arguments, buster_a64_semantic_value_program_count() == 296);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_semantic_executable_count() == 0);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_validate());
    BUSTER_TEST(arguments, a64_vm_raw_round_trip_all_rows());

    BusterA64SemanticVMValue invalid = buster_a64_semantic_vm_value_unsigned(1, 0);
    BUSTER_TEST(arguments, invalid.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    invalid = buster_a64_semantic_vm_value_unsigned(1, 65);
    BUSTER_TEST(arguments, invalid.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BusterA64SemanticVMValue boundary = buster_a64_semantic_vm_value_unsigned(UINT64_C(0xff), 8);
    BUSTER_TEST(arguments, boundary.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER && boundary.payload == UINT64_C(0xff));
    invalid = buster_a64_semantic_vm_value_unsigned(UINT64_C(0x100), 8);
    BUSTER_TEST(arguments, invalid.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(-128, 8).kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(127, 8).kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(-129, 8).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(128, 8).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(INT64_MIN, 64).kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_signed(INT64_MAX, 64).kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIGNED_INTEGER);
    invalid = buster_a64_semantic_vm_value_bits(1, UINT64_MAX, 8);
    BUSTER_TEST(arguments, invalid.kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_gpr(31, 64, true, true).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_gpr(30, 64, true, false).kind == BUSTER_A64_SEMANTIC_VM_VALUE_INVALID);
    BUSTER_TEST(arguments, buster_a64_semantic_vm_value_gpr(31, 32, false, true).kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER);

    BusterA64SemanticVMValue output = buster_a64_semantic_vm_value_unsigned(UINT64_C(0x1234), 16);
    BusterA64SemanticVMValue input = buster_a64_semantic_vm_value_signed(INT64_MAX, 64);
    BusterA64SemanticVMInstruction instruction = {.op = BUSTER_A64_SEMANTIC_VM_OP_SCALE_MUL, .constant = 2};
    BusterA64SemanticVMValue before = output;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &input, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_RANGE);
    BUSTER_TEST(arguments, memcmp(&output, &before, sizeof(output)) == 0);
    instruction.op = BUSTER_A64_SEMANTIC_VM_OP_INVALID;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &input, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_UNSUPPORTED);
    BUSTER_TEST(arguments, memcmp(&output, &before, sizeof(output)) == 0);
    input = buster_a64_semantic_vm_value_signed(INT64_MIN, 64);
    instruction.op = BUSTER_A64_SEMANTIC_VM_OP_SUB_FROM_CONST;
    instruction.constant = 128;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &input, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_RANGE);
    BUSTER_TEST(arguments, memcmp(&output, &before, sizeof(output)) == 0);

    instruction = (BusterA64SemanticVMInstruction){.op = BUSTER_A64_SEMANTIC_VM_OP_FIXED_LITERAL, .width = 8, .constant = 0x100};
    before = output;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, NULL, 0, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_RANGE);
    BUSTER_TEST(arguments, memcmp(&output, &before, sizeof(output)) == 0);

    input = buster_a64_semantic_vm_value_unsigned(UINT64_MAX, 64);
    instruction.op = BUSTER_A64_SEMANTIC_VM_OP_CONCAT;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &input, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, output.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER && output.width == 64 && output.payload == UINT64_MAX);

    BusterA64SemanticVMValue gpr = buster_a64_semantic_vm_value_gpr(31, 64, true, false);
    instruction = (BusterA64SemanticVMInstruction){.op = BUSTER_A64_SEMANTIC_VM_OP_REGISTER_ADD_MOD32, .constant = 1};
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &gpr, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, output.kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER && output.payload == 0 && output.flags == 0);
    BusterA64SemanticVMValue simd = {.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST, .width = 5, .payload = 31};
    BUSTER_TEST(arguments, buster_a64_semantic_vm_apply(instruction, &simd, 1, 0, 0, &output) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, output.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LIST && output.payload == 0);

    u32 form_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:ABS_asimdmisc_R"), 0, &form_id));
    BusterA64SemanticForm form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(form_id, &form));
    BusterA64SemanticVMFields fields = {.count = form.field_count};
    u32 table_id = UINT32_MAX;
    BUSTER_TEST(arguments, a64_vm_find_transform(form_id, BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE, &table_id));
    BusterA64SemanticVMValue table_result = output;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(form_id, table_id, &fields, &table_result) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, table_result.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION);

    /* Arm's fixed-width bitfield cells must remain bit patterns even when
       their spelling is also a decimal integer.  ADDHN's reserved ``11``
       size row is wildcarded over Q, so both Q values must reject without
       clobbering the caller's output. */
    u32 addhn_id = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:ADDHN_asimddiff_N"), 0, &addhn_id));
    BusterA64SemanticForm addhn_form = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_form(addhn_id, &addhn_form));
    BusterA64SemanticVMFields addhn_fields = {.count = addhn_form.field_count};
    u32 addhn_table_id = UINT32_MAX;
    BUSTER_TEST(arguments, a64_vm_find_table_by_headers(addhn_id, S8("size"), S8("Q"), &addhn_table_id));
    BusterA64SemanticVMValue addhn_sentinel = buster_a64_semantic_vm_value_unsigned(UINT64_C(0x5a), 8);
    BusterA64SemanticVMValue addhn_before = addhn_sentinel;
    BUSTER_TEST(arguments, a64_vm_set_field(addhn_id, &addhn_fields, S8("size"), 2));
    BUSTER_TEST(arguments, a64_vm_set_field(addhn_id, &addhn_fields, S8("Q"), 0));
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(addhn_id, addhn_table_id, &addhn_fields, &addhn_sentinel) == BUSTER_A64_SEMANTIC_VM_STATUS_OK &&
                           addhn_sentinel.kind == BUSTER_A64_SEMANTIC_VM_VALUE_ENUMERATION && a64_vm_string_is(addhn_sentinel.text, S8("2S")));
    addhn_sentinel = addhn_before;
    BUSTER_TEST(arguments, a64_vm_set_field(addhn_id, &addhn_fields, S8("size"), 3));
    BUSTER_TEST(arguments, a64_vm_set_field(addhn_id, &addhn_fields, S8("Q"), 0));
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(addhn_id, addhn_table_id, &addhn_fields, &addhn_sentinel) == BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED);
    BUSTER_TEST(arguments, memcmp(&addhn_sentinel, &addhn_before, sizeof(addhn_sentinel)) == 0);
    BUSTER_TEST(arguments, a64_vm_set_field(addhn_id, &addhn_fields, S8("Q"), 1));
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(addhn_id, addhn_table_id, &addhn_fields, &addhn_sentinel) == BUSTER_A64_SEMANTIC_VM_STATUS_RESERVED);
    BUSTER_TEST(arguments, memcmp(&addhn_sentinel, &addhn_before, sizeof(addhn_sentinel)) == 0);

    u32 addhn_q_table_id = UINT32_MAX;
    BusterA64SemanticValue addhn_q_entry = {0};
    BusterA64SemanticValueAtom addhn_q_atom = {0};
    BusterA64SemanticValue addhn_q_entry_one = {0};
    BusterA64SemanticValueAtom addhn_q_atom_one = {0};
    BUSTER_TEST(arguments, a64_vm_find_transform(addhn_id, BUSTER_A64_SEMANTIC_TRANSFORM_VALUE_TABLE, &addhn_q_table_id) &&
                           buster_a64_semantic_transform_value(addhn_q_table_id, 0, &addhn_q_entry) && addhn_q_entry.key_count == 1 &&
                           buster_a64_semantic_value_atom(addhn_q_entry.key_first, &addhn_q_atom) &&
                           addhn_q_atom.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER && addhn_q_atom.integer == 0 &&
                           buster_a64_semantic_transform_value(addhn_q_table_id, 1, &addhn_q_entry_one) && addhn_q_entry_one.key_count == 1 &&
                           buster_a64_semantic_value_atom(addhn_q_entry_one.key_first, &addhn_q_atom_one) &&
                           addhn_q_atom_one.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER && addhn_q_atom_one.integer == 1);
    bool addhn_wildcard = false;
    for (u32 value_index = 0; value_index < 16 && !addhn_wildcard; value_index += 1)
    {
        BusterA64SemanticValue value = {0};
        BusterA64SemanticValueAtom size_atom = {0};
        BusterA64SemanticValueAtom q_atom = {0};
        if (!buster_a64_semantic_transform_value(addhn_table_id, value_index, &value) || value.key_count != 2 ||
            !buster_a64_semantic_value_atom(value.key_first, &size_atom) || !buster_a64_semantic_value_atom(value.key_first + 1, &q_atom)) continue;
        addhn_wildcard = size_atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS && a64_vm_string_is(size_atom.text, S8("11")) &&
                         q_atom.kind == BUSTER_A64_SEMANTIC_VALUE_BITS && a64_vm_string_is(q_atom.text, S8("x"));
    }
    BUSTER_TEST(arguments, addhn_wildcard);

    /* Decimal result cells are not bitfield keys: the cmode table keeps its
       shift amounts as integer 0, 8, 16, and 24 while the two-bit keys above
       are exact patterns. */
    u32 bic_id = UINT32_MAX;
    u32 bic_table_id = UINT32_MAX;
    BusterA64SemanticValue bic_entry = {0};
    BusterA64SemanticValueAtom bic_result = {0};
    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:BIC_asimdimm_L_sl"), 0, &bic_id) &&
                           a64_vm_find_table_by_header(bic_id, S8("cmode[2:1]"), &bic_table_id) &&
                           buster_a64_semantic_transform_value(bic_table_id, 1, &bic_entry) && bic_entry.result_count == 1 &&
                           buster_a64_semantic_value_atom(bic_entry.result_first, &bic_result) &&
                           bic_result.kind == BUSTER_A64_SEMANTIC_VALUE_INTEGER && bic_result.integer == 8);

    BUSTER_TEST(arguments, buster_a64_semantic_form(208, &form));
    fields = (BusterA64SemanticVMFields){.count = form.field_count};
    BUSTER_TEST(arguments, a64_vm_set_field(208, &fields, S8("imm5"), 1));
    BUSTER_TEST(arguments, a64_vm_find_program_table_transform(208, &table_id));
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(208, table_id, &fields, &table_result) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, table_result.kind == BUSTER_A64_SEMANTIC_VM_VALUE_UNSIGNED_INTEGER && table_result.payload == 0);

    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:SHL_asisdshf_R"), 0, &form_id));
    BUSTER_TEST(arguments, buster_a64_semantic_form(form_id, &form));
    fields = (BusterA64SemanticVMFields){.count = form.field_count};
    BUSTER_TEST(arguments, a64_vm_set_field(form_id, &fields, S8("immh"), 4));
    BUSTER_TEST(arguments, a64_vm_set_field(form_id, &fields, S8("immb"), 0));
    u32 integer_id = UINT32_MAX;
    BUSTER_TEST(arguments, a64_vm_find_transform(form_id, BUSTER_A64_SEMANTIC_TRANSFORM_INTEGER_DECODE, &integer_id));
    BusterA64SemanticVMValue decoded = output;
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(form_id, integer_id, &fields, &decoded) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, (s64)decoded.payload == -32);

    BUSTER_TEST(arguments, buster_a64_semantic_find_form(S8("arm-a64@2026-06:FMAXNMV_asimdall_only_H"), 0, &form_id));
    BUSTER_TEST(arguments, buster_a64_semantic_form(form_id, &form));
    fields = (BusterA64SemanticVMFields){.count = form.field_count};
    BUSTER_TEST(arguments, a64_vm_set_field(form_id, &fields, S8("Q"), 1));
    u32 shared_id = UINT32_MAX;
    BUSTER_TEST(arguments, a64_vm_find_transform(form_id, BUSTER_A64_SEMANTIC_TRANSFORM_SHARED_DECODE, &shared_id));
    BUSTER_TEST(arguments, buster_a64_semantic_vm_eval_transform(form_id, shared_id, &fields, &decoded) == BUSTER_A64_SEMANTIC_VM_STATUS_OK);
    BUSTER_TEST(arguments, decoded.kind == BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_ARRANGEMENT && decoded.payload == 1 && decoded.aux == 8);
    return result;
}

#endif
