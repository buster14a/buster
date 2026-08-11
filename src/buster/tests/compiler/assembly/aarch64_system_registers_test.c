#include <buster/tests/compiler/assembly/aarch64_system_registers_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_system_registers.h>

UnitTestResult aarch64_system_registers_tests(UnitTestArguments* arguments)
{
    (void)arguments;
    UnitTestResult result = {0};
    Aarch64SystemRegisterCensus census = aarch64_system_register_census();
    BUSTER_TEST(arguments, census.relevant_mechanism_count == 1400);
    BUSTER_TEST(arguments, census.accepted_mechanism_count == 402);
    BUSTER_TEST(arguments, census.fixed_count == 392 && census.parameterized_count == 10);
    BUSTER_TEST(arguments, census.fixed_target_name_count == 202 && census.fixed_encoding_count == 201);
    BUSTER_TEST(arguments, census.readable_fixed_name_count == 200 && census.writable_fixed_name_count == 138 && census.both_fixed_name_count == 136);
    BUSTER_TEST(arguments, census.source_fixed_row_count == 392 && census.source_parameterized_row_count == 8 && census.source_raw_s3_row_count == 2);
    BUSTER_TEST(arguments, aarch64_system_register_count() == 402);
    bool profile_pair_present = false;
    for (u32 index = 0; index < aarch64_system_register_count(); index += 1)
    {
        Aarch64SystemRegister row = {0};
        if (aarch64_system_register_at(index, &row) && (row.mechanism == AARCH64_SYSTEM_REGISTER_MRRS || row.mechanism == AARCH64_SYSTEM_REGISTER_MSRR_REGISTER))
            profile_pair_present = true;
    }
    BUSTER_TEST(arguments, !profile_pair_present);

    Aarch64SystemRegisterLookup lookup = {0};
    BUSTER_TEST(arguments, aarch64_system_register_lookup_name(S8("DBGDTRRX_EL0"), &lookup));
    BUSTER_TEST(arguments, lookup.mode == AARCH64_SYSTEM_REGISTER_MODE_READ);
    BUSTER_TEST(arguments, aarch64_system_register_lookup_name(S8("DBGDTRTX_EL0"), &lookup));
    BUSTER_TEST(arguments, lookup.mode == AARCH64_SYSTEM_REGISTER_MODE_WRITE);
    BUSTER_TEST(arguments, aarch64_system_register_name_is_eligible(S8("DBGDTRRX_EL0"), AARCH64_SYSTEM_REGISTER_MODE_READ));
    BUSTER_TEST(arguments, !aarch64_system_register_name_is_eligible(S8("DBGDTRRX_EL0"), AARCH64_SYSTEM_REGISTER_MODE_WRITE));
    BUSTER_TEST(arguments, aarch64_system_register_lookup_name(S8("dbgdtrrx_el0"), &lookup));
    BUSTER_TEST(arguments, !aarch64_system_register_name_is_eligible(S8("DBGDTRRX_EL0"), AARCH64_SYSTEM_REGISTER_MODE_NONE));
    BUSTER_TEST(arguments, !aarch64_system_register_name_is_eligible(S8("DBGDTRRX_EL0"), (Aarch64SystemRegisterMode)4));
    BUSTER_TEST(arguments, aarch64_system_register_lookup_encoding(49281, &lookup) && lookup.canonical_name.length == S8("ACTLR_EL1").length);

    u16 packed = 0;
    BUSTER_TEST(arguments, aarch64_system_register_parse_raw_s3(S8("S3_3_C7_C5_0"), &packed));
    BUSTER_TEST(arguments, ((packed >> 14) & 3u) == 3u && ((packed >> 11) & 7u) == 3u && ((packed >> 7) & 15u) == 7u &&
                               ((packed >> 3) & 15u) == 5u && (packed & 7u) == 0u);
    BUSTER_TEST(arguments, !aarch64_system_register_parse_raw_s3(S8("S3_8_C7_C5_0"), &packed));
    BUSTER_TEST(arguments, !aarch64_system_register_parse_raw_s3(S8("S3_3_C16_C5_0"), &packed));
    String8 preserved = S8("preserved");
    u64 raw_format_position = arguments->arena->position;
    BUSTER_TEST(arguments, !aarch64_system_register_format_raw_s3(arguments->arena, 0x8000, &preserved));
    BUSTER_TEST(arguments, arguments->arena->position == raw_format_position && preserved.length == S8("preserved").length &&
                               memcmp(preserved.pointer, S8("preserved").pointer, preserved.length) == 0);

    u32 mrs = 0, msr = 0;
    BUSTER_TEST(arguments, aarch64_system_register_encode_mrs(0u, 31, &mrs) == false);
    BUSTER_TEST(arguments, aarch64_system_register_lookup_name(S8("DBGDTRRX_EL0"), &lookup));
    BUSTER_TEST(arguments, aarch64_system_register_encode_mrs(lookup.packed_encoding, 31, &mrs));
    BUSTER_TEST(arguments, aarch64_system_register_encode_msr(lookup.packed_encoding, 31, &msr));
    bool is_read = false; u16 decoded = 0; u32 rt = 0;
    BUSTER_TEST(arguments, aarch64_system_register_decode_word(mrs, &is_read, &decoded, &rt) && is_read && decoded == lookup.packed_encoding && rt == 31);
    BUSTER_TEST(arguments, aarch64_system_register_decode_word(msr, &is_read, &decoded, &rt) && !is_read && decoded == lookup.packed_encoding && rt == 31);

    String8 expanded = {0}; u16 expanded_encoding = 0;
    BUSTER_TEST(arguments, aarch64_system_register_expand_name_encoding(arguments->arena, S8("DBGBCR<n>_EL1"), 15, &expanded, &expanded_encoding));
    BUSTER_TEST(arguments, expanded.length == S8("DBGBCR15_EL1").length && memcmp(expanded.pointer, S8("DBGBCR15_EL1").pointer, expanded.length) == 0);
    BUSTER_TEST(arguments, expanded_encoding == 0x807d);
    BUSTER_TEST(arguments, aarch64_system_register_lookup_expanded_name(S8("dbgbcr15_el1"), &lookup) && lookup.packed_encoding == expanded_encoding &&
                               lookup.mode == AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE);
    BUSTER_TEST(arguments, aarch64_system_register_name_is_eligible(S8("DBGBCR15_EL1"), AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE));
    u64 expansion_failure_position = arguments->arena->position;
    BUSTER_TEST(arguments, !aarch64_system_register_expand_name_encoding(arguments->arena, S8("DBGBCR<n>_EL1"), 16, &expanded, &expanded_encoding));
    BUSTER_TEST(arguments, arguments->arena->position == expansion_failure_position);

    u32 mrrs = 0, msrr = 0, rt2 = 0;
    BUSTER_TEST(arguments, aarch64_system_register_encode_mrrs(0xc100, 2, &mrrs));
    BUSTER_TEST(arguments, aarch64_system_register_encode_msrr(0xc100, 2, &msrr));
    BUSTER_TEST(arguments, mrrs == 0xd5782002u && msrr == 0xd5582002u);
    BUSTER_TEST(arguments, aarch64_system_register_decode_pair_word(mrrs, &is_read, &decoded, &rt, &rt2) && is_read && decoded == 0xc100 && rt == 2 && rt2 == 3);
    BUSTER_TEST(arguments, aarch64_system_register_decode_pair_word(msrr, &is_read, &decoded, &rt, &rt2) && !is_read && decoded == 0xc100 && rt == 2 && rt2 == 3);
    BUSTER_TEST(arguments, !aarch64_system_register_encode_mrrs(0xc100, 3, &mrrs));
    BUSTER_TEST(arguments, !aarch64_system_register_encode_mrrs(0x8100, 2, &mrrs));
    BUSTER_TEST(arguments, !aarch64_system_register_encode_mrrs(0xc100, 31, &mrrs));

    String8 before = S8("immutable");
    Aarch64SystemRegisterLookup unchanged = {.canonical_name = before, .packed_encoding = 0x1234};
    BUSTER_TEST(arguments, !aarch64_system_register_lookup_name(S8("NO_SUCH_SYSREG"), &unchanged));
    BUSTER_TEST(arguments, unchanged.canonical_name.pointer == before.pointer && unchanged.canonical_name.length == before.length && unchanged.packed_encoding == 0x1234);
    return result;
}

#endif
