#include <buster/tests/compiler/assembly/aarch64_alias_projection_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_alias_projection.h>

static Target a64_alias_projection_test_target(void)
{
    return (Target){.cpu_arch = CPU_ARCH_AARCH64,
                    .cpu_model = CPU_MODEL_A64_APPLE_M1,
                    .os = OPERATING_SYSTEM_MACOS,
                    .cpu_features_explicit = true,
                    .cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1)};
}

static bool a64_alias_projection_string_is(String8 actual, char8 const* expected)
{
    if (!expected)
    {
        return false;
    }
    u32 length = 0;
    while (expected[length])
    {
        length += 1;
    }
    if (actual.length != length)
    {
        return false;
    }
    for (u32 index = 0; index < length; index += 1)
    {
        if (actual.pointer[index] != expected[index])
        {
            return false;
        }
    }
    return true;
}

UnitTestResult aarch64_alias_projection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Target target = a64_alias_projection_test_target();
    BUSTER_TEST(arguments, buster_a64_alias_projection_schema_version() == 1);
    BUSTER_TEST(arguments, buster_a64_alias_count() == 172 && buster_a64_alias_canonical_count() == 1523);
    BUSTER_TEST(arguments, buster_a64_alias_generic_executable_count() == 106);
    BUSTER_TEST(arguments,
                a64_alias_projection_string_is(buster_a64_alias_denominator_sha256(), "fa030a65c5be661813f50a92f3027e152ab9c1b55701f851c6142390df5c25a8"));
    BUSTER_TEST(arguments, buster_a64_alias_validate());

    u32 owner_counts[7] = {0};
    bool rows_valid = true;
    for (u32 ordinal = 0; ordinal < buster_a64_alias_count(); ordinal += 1)
    {
        BusterA64AliasRowInfo row = {0};
        rows_valid = rows_valid && buster_a64_alias_row(ordinal, &row) && row.alias_ordinal == ordinal && row.alias_form_id != row.target_form_id;
        if (rows_valid && row.target_owner < 7)
        {
            owner_counts[row.target_owner] += 1;
        }
        BusterA64AliasRowInfo by_form = {0};
        rows_valid = rows_valid && buster_a64_alias_row_by_form(row.alias_form_id, &by_form) && by_form.alias_ordinal == ordinal;
    }
    BUSTER_TEST(arguments, rows_valid);
    BUSTER_TEST(arguments, owner_counts[0] == 64 && owner_counts[1] == 59 && owner_counts[2] == 21 && owner_counts[3] == 10 && owner_counts[4] == 9 &&
                               owner_counts[5] == 7 && owner_counts[6] == 2);

    u32 asr_form = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_alias_find(S8("arm-a64@2026-06:ASR_ASRV_32_dp_2src"), 0, &asr_form));
    BusterA64AliasInstruction asr = {.alias_form_id = asr_form, .operand_count = 3};
    asr.operands[0] = buster_a64_semantic_vm_value_gpr(0, 32, false, false);
    asr.operands[1] = buster_a64_semantic_vm_value_gpr(1, 32, false, false);
    asr.operands[2] = buster_a64_semantic_vm_value_gpr(2, 32, false, false);
    u32 word = UINT32_C(0xdeadbeef);
    BusterA64AliasStatus encode_status = buster_a64_alias_encode(target, &asr, &word);
    BUSTER_TEST(arguments, encode_status == BUSTER_A64_ALIAS_STATUS_OK);
    BUSTER_TEST(arguments, word == UINT32_C(0x1ac22820));
    BusterA64AliasResult decoded = {0};
    BUSTER_TEST(arguments, buster_a64_alias_decode_row(target, asr_form, word, &decoded) == BUSTER_A64_ALIAS_STATUS_OK && decoded.alias_form_id == asr_form &&
                               decoded.operands[0].payload == 0 && decoded.operands[2].payload == 2);

    asr.operands[0] = buster_a64_semantic_vm_value_gpr(32, 32, false, false);
    u32 unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &asr, &unchanged) != BUSTER_A64_ALIAS_STATUS_OK && unchanged == UINT32_C(0xfeedface));
    BUSTER_TEST(arguments, buster_a64_alias_decode_row(target, asr_form, UINT32_C(0), &decoded) != BUSTER_A64_ALIAS_STATUS_OK);
    return result;
}

#endif
