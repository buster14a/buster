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
    bool result = expected != 0;
    if (result)
    {
        u32 length = 0;
        while (expected[length])
        {
            length += 1;
        }
        result = actual.length == length;
        for (u32 index = 0; index < length && result; index += 1)
        {
            result = actual.pointer[index] == expected[index];
        }
    }

    return result;
}

UnitTestResult aarch64_alias_projection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    Target target = a64_alias_projection_test_target();
    BUSTER_TEST(arguments, buster_a64_alias_projection_schema_version() == 1);
    BUSTER_TEST(arguments, buster_a64_alias_count() == 172 && buster_a64_alias_canonical_count() == 1523);
    BUSTER_TEST(arguments, buster_a64_alias_generic_executable_count() == 104);
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

    asr.operands[0] = buster_a64_semantic_vm_value_gpr(31, 32, false, true);
    unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &asr, &unchanged) == BUSTER_A64_ALIAS_STATUS_OK);
    asr.operands[0] = buster_a64_semantic_vm_value_gpr(31, 32, true, false);
    unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &asr, &unchanged) != BUSTER_A64_ALIAS_STATUS_OK && unchanged == UINT32_C(0xfeedface));

    u32 memory_form = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_alias_find(S8("arm-a64@2026-06:STADDB_LDADDB_32_memop"), 0, &memory_form));
    BusterA64AliasInstruction memory = {.alias_form_id = memory_form, .operand_count = 2};
    memory.operands[0] = buster_a64_semantic_vm_value_gpr(0, 32, false, false);
    memory.operands[1] = buster_a64_semantic_vm_value_gpr(31, 64, true, false);
    u32 memory_word = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &memory, &memory_word) == BUSTER_A64_ALIAS_STATUS_OK);
    BusterA64AliasResult memory_decoded = {0};
    BusterA64AliasStatus memory_decode_status = buster_a64_alias_decode_row(target, memory_form, memory_word, &memory_decoded);
    BUSTER_TEST(arguments, memory_decode_status == BUSTER_A64_ALIAS_STATUS_OK &&
                               memory_decoded.operands[1].kind == BUSTER_A64_SEMANTIC_VM_VALUE_GPR_REGISTER &&
                               (memory_decoded.operands[1].flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_SP) != 0 &&
                               (memory_decoded.operands[1].flags & BUSTER_A64_SEMANTIC_VM_VALUE_FLAG_ZR) == 0);
    memory.operands[1] = buster_a64_semantic_vm_value_gpr(31, 64, false, true);
    unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &memory, &unchanged) != BUSTER_A64_ALIAS_STATUS_OK && unchanged == UINT32_C(0xfeedface));

    Target non_m1 = target;
    non_m1.cpu_model = CPU_MODEL_A64_GENERIC;
    unchanged = UINT32_C(0xfeedface);
    asr.operands[0] = buster_a64_semantic_vm_value_gpr(0, 32, false, false);
    BUSTER_TEST(arguments, buster_a64_alias_encode(non_m1, &asr, &unchanged) == BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH && unchanged == UINT32_C(0xfeedface));
    BusterA64AliasResult target_marker = {.status = BUSTER_A64_ALIAS_STATUS_RANGE, .alias_form_id = UINT32_C(0xabcdef01)};
    BUSTER_TEST(arguments, buster_a64_alias_decode_row(non_m1, asr_form, word, &target_marker) == BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH &&
                               target_marker.status == BUSTER_A64_ALIAS_STATUS_RANGE && target_marker.alias_form_id == UINT32_C(0xabcdef01));
    Target invalid_features = target;
    invalid_features.cpu_features = target_cpu_features_remove(invalid_features.cpu_features, TARGET_CPU_FEATURE_AARCH64_NEON);
    unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(invalid_features, &asr, &unchanged) == BUSTER_A64_ALIAS_STATUS_TARGET_MISMATCH && unchanged == UINT32_C(0xfeedface));

    u32 unsupported_form = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_alias_find(S8("arm-a64@2026-06:AT_SYS_CR_systeminstrs"), 0, &unsupported_form));
    BUSTER_TEST(arguments, !buster_a64_alias_preference_supported(unsupported_form));
    BusterA64AliasResult unsupported_result = {.status = BUSTER_A64_ALIAS_STATUS_RANGE, .alias_form_id = UINT32_C(0xabcdef01)};
    BUSTER_TEST(arguments, buster_a64_alias_decode_row(target, unsupported_form, UINT32_C(0xd5087800), &unsupported_result) != BUSTER_A64_ALIAS_STATUS_OK &&
                               unsupported_result.status == BUSTER_A64_ALIAS_STATUS_RANGE && unsupported_result.alias_form_id == UINT32_C(0xabcdef01));

    u32 mov_umov_form = UINT32_MAX;
    BUSTER_TEST(arguments, buster_a64_alias_find(S8("arm-a64@2026-06:MOV_UMOV_asimdins_W_w"), 0, &mov_umov_form));
    BUSTER_TEST(arguments, buster_a64_alias_preference_supported(mov_umov_form));
    BusterA64AliasInstruction mov_umov = {.alias_form_id = mov_umov_form, .operand_count = 3};
    mov_umov.operands[0] = buster_a64_semantic_vm_value_gpr(0, 32, false, false);
    mov_umov.operands[1] = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_REGISTER, .width = 5, .payload = 1};
    mov_umov.operands[2] = (BusterA64SemanticVMValue){.kind = BUSTER_A64_SEMANTIC_VM_VALUE_SIMD_LANE, .width = 2, .payload = 0};
    unchanged = UINT32_C(0xfeedface);
    BUSTER_TEST(arguments, buster_a64_alias_encode(target, &mov_umov, &unchanged) == BUSTER_A64_ALIAS_STATUS_UNSUPPORTED && unchanged == UINT32_C(0xfeedface));
    return result;
}

#endif
