#include <buster/tests/compiler/assembly/aarch64_system_registers_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_system_registers.h>

typedef struct Aarch64SystemRegisterCollisionExpectation Aarch64SystemRegisterCollisionExpectation;
struct Aarch64SystemRegisterCollisionExpectation
{
    String8 name;
    u16 packed_encoding;
};

static Aarch64SystemRegisterCollisionExpectation aarch64_system_register_collision_expectations[] = {
    {S8_INITIALIZER("AFSR0_EL1"), 0xc288}, {S8_INITIALIZER("AFSR0_EL2"), 0xe288},
    {S8_INITIALIZER("AFSR1_EL1"), 0xc289}, {S8_INITIALIZER("AFSR1_EL2"), 0xe289},
    {S8_INITIALIZER("AMAIR_EL1"), 0xc518}, {S8_INITIALIZER("AMAIR_EL2"), 0xe518},
    {S8_INITIALIZER("CNTHCTL_EL2"), 0xe708}, {S8_INITIALIZER("CNTHP_CTL_EL2"), 0xe711},
    {S8_INITIALIZER("CNTHP_CVAL_EL2"), 0xe712}, {S8_INITIALIZER("CNTHP_TVAL_EL2"), 0xe710},
    {S8_INITIALIZER("CNTHV_CTL_EL2"), 0xe719}, {S8_INITIALIZER("CNTHV_CVAL_EL2"), 0xe71a},
    {S8_INITIALIZER("CNTHV_TVAL_EL2"), 0xe718}, {S8_INITIALIZER("CNTKCTL_EL1"), 0xc708},
    {S8_INITIALIZER("CNTP_CTL_EL0"), 0xdf11}, {S8_INITIALIZER("CNTP_CVAL_EL0"), 0xdf12},
    {S8_INITIALIZER("CNTP_TVAL_EL0"), 0xdf10}, {S8_INITIALIZER("CNTV_CTL_EL0"), 0xdf19},
    {S8_INITIALIZER("CNTV_CVAL_EL0"), 0xdf1a}, {S8_INITIALIZER("CNTV_TVAL_EL0"), 0xdf18},
    {S8_INITIALIZER("CONTEXTIDR_EL1"), 0xc681}, {S8_INITIALIZER("CPACR_EL1"), 0xc082},
    {S8_INITIALIZER("CPTR_EL2"), 0xe08a}, {S8_INITIALIZER("ELR_EL1"), 0xc201},
    {S8_INITIALIZER("ELR_EL2"), 0xe201}, {S8_INITIALIZER("ESR_EL1"), 0xc290},
    {S8_INITIALIZER("ESR_EL2"), 0xe290}, {S8_INITIALIZER("FAR_EL1"), 0xc300},
    {S8_INITIALIZER("FAR_EL2"), 0xe300}, {S8_INITIALIZER("MAIR_EL1"), 0xc510},
    {S8_INITIALIZER("MAIR_EL2"), 0xe510}, {S8_INITIALIZER("SCTLR_EL1"), 0xc080},
    {S8_INITIALIZER("SCTLR_EL2"), 0xe080}, {S8_INITIALIZER("SPSR_EL1"), 0xc200},
    {S8_INITIALIZER("SPSR_EL2"), 0xe200}, {S8_INITIALIZER("TCR_EL1"), 0xc102},
    {S8_INITIALIZER("TCR_EL2"), 0xe102}, {S8_INITIALIZER("TTBR0_EL1"), 0xc100},
    {S8_INITIALIZER("TTBR0_EL2"), 0xe100}, {S8_INITIALIZER("TTBR1_EL1"), 0xc101},
    {S8_INITIALIZER("TTBR1_EL2"), 0xe101}, {S8_INITIALIZER("VBAR_EL1"), 0xc600},
    {S8_INITIALIZER("VBAR_EL2"), 0xe600}, {S8_INITIALIZER("VDISR_EL2"), 0xe609},
    {S8_INITIALIZER("VMPIDR_EL2"), 0xe005}, {S8_INITIALIZER("VPIDR_EL2"), 0xe000},
};

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

    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(aarch64_system_register_collision_expectations); index += 1)
    {
        Aarch64SystemRegisterCollisionExpectation expectation = aarch64_system_register_collision_expectations[index];
        Aarch64SystemRegisterLookup collision = {.canonical_name = S8("unchanged"), .packed_encoding = 0x1234, .mode = AARCH64_SYSTEM_REGISTER_MODE_READ,
                                                 .alias_count = 7, .mechanism_count = 9};
        bool found_collision = aarch64_system_register_lookup_name(expectation.name, &collision);
        BUSTER_TEST(arguments, found_collision && collision.packed_encoding == expectation.packed_encoding &&
                               collision.mode == AARCH64_SYSTEM_REGISTER_MODE_READ_WRITE && collision.alias_count == 0 && collision.mechanism_count != 0);
    }

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

    String8 null_string = {.pointer = 0, .length = 1};
    Aarch64SystemRegisterLookup null_lookup_before = {.canonical_name = S8("unchanged"), .packed_encoding = 0x1234,
                                                       .mode = AARCH64_SYSTEM_REGISTER_MODE_READ, .alias_count = 3, .mechanism_count = 4,
                                                       .parameterized = 1, .raw_s3 = 1};
    Aarch64SystemRegisterLookup null_lookup = null_lookup_before;
    BUSTER_TEST(arguments, !aarch64_system_register_lookup_name(null_string, &null_lookup) && memcmp(&null_lookup, &null_lookup_before, sizeof(null_lookup)) == 0);
    BUSTER_TEST(arguments, !aarch64_system_register_lookup_expanded_name(null_string, &null_lookup) && memcmp(&null_lookup, &null_lookup_before, sizeof(null_lookup)) == 0);
    BUSTER_TEST(arguments, !aarch64_system_register_name_is_eligible(null_string, AARCH64_SYSTEM_REGISTER_MODE_READ));
    u16 null_raw_before = 0x4321, null_raw = null_raw_before;
    BUSTER_TEST(arguments, !aarch64_system_register_parse_raw_s3(null_string, &null_raw) && null_raw == null_raw_before);
    String8 null_expanded_before = S8("unchanged"), null_expanded = null_expanded_before;
    u16 null_expanded_encoding_before = 0x3456, null_expanded_encoding = null_expanded_encoding_before;
    u64 null_expand_position = arguments->arena->position;
    BUSTER_TEST(arguments, !aarch64_system_register_expand_name(arguments->arena, null_string, 1, &null_expanded) &&
                               arguments->arena->position == null_expand_position && null_expanded.pointer == null_expanded_before.pointer &&
                               null_expanded.length == null_expanded_before.length);
    BUSTER_TEST(arguments, !aarch64_system_register_expand_name_encoding(arguments->arena, null_string, 1, &null_expanded, &null_expanded_encoding) &&
                               arguments->arena->position == null_expand_position && null_expanded.pointer == null_expanded_before.pointer &&
                               null_expanded.length == null_expanded_before.length && null_expanded_encoding == null_expanded_encoding_before);

    u16 sentinel_raw_before = 0x2345, sentinel_raw = sentinel_raw_before;
    BUSTER_TEST(arguments, !aarch64_system_register_parse_raw_s3(S8("S3_7_C15_C15_7"), &sentinel_raw) && sentinel_raw == sentinel_raw_before);
    String8 sentinel_text_before = S8("unchanged"), sentinel_text = sentinel_text_before;
    u64 sentinel_format_position = arguments->arena->position;
    BUSTER_TEST(arguments, !aarch64_system_register_format_raw_s3(arguments->arena, 0xffff, &sentinel_text) &&
                               arguments->arena->position == sentinel_format_position && sentinel_text.pointer == sentinel_text_before.pointer &&
                               sentinel_text.length == sentinel_text_before.length);
    Aarch64SystemRegisterLookup sentinel_lookup_before = null_lookup_before, sentinel_lookup = sentinel_lookup_before;
    BUSTER_TEST(arguments, !aarch64_system_register_lookup_encoding(0xffff, &sentinel_lookup) && memcmp(&sentinel_lookup, &sentinel_lookup_before, sizeof(sentinel_lookup)) == 0);
    u32 sentinel_word_before = 0x12345678, sentinel_word = sentinel_word_before;
    BUSTER_TEST(arguments, !aarch64_system_register_encode_mrs(0xffff, 0, &sentinel_word) && sentinel_word == sentinel_word_before);
    sentinel_word = sentinel_word_before;
    BUSTER_TEST(arguments, !aarch64_system_register_encode_msr(0xffff, 0, &sentinel_word) && sentinel_word == sentinel_word_before);
    sentinel_word = sentinel_word_before;
    BUSTER_TEST(arguments, !aarch64_system_register_encode_mrrs(0xffff, 0, &sentinel_word) && sentinel_word == sentinel_word_before);
    sentinel_word = sentinel_word_before;
    BUSTER_TEST(arguments, !aarch64_system_register_encode_msrr(0xffff, 0, &sentinel_word) && sentinel_word == sentinel_word_before);
    bool sentinel_read_before = true, sentinel_read = sentinel_read_before;
    u16 sentinel_decoded_before = 0x2345, sentinel_decoded = sentinel_decoded_before;
    u32 sentinel_rt_before = 19, sentinel_rt = sentinel_rt_before;
    BUSTER_TEST(arguments, !aarch64_system_register_decode_word(0xd53fffe5u, &sentinel_read, &sentinel_decoded, &sentinel_rt) &&
                               sentinel_read == sentinel_read_before && sentinel_decoded == sentinel_decoded_before && sentinel_rt == sentinel_rt_before);
    sentinel_read = sentinel_read_before;
    sentinel_decoded = sentinel_decoded_before;
    sentinel_rt = sentinel_rt_before;
    u32 sentinel_rt2_before = 21, sentinel_rt2 = sentinel_rt2_before;
    BUSTER_TEST(arguments, !aarch64_system_register_decode_pair_word(0xd57fffe4u, &sentinel_read, &sentinel_decoded, &sentinel_rt, &sentinel_rt2) &&
                               sentinel_read == sentinel_read_before && sentinel_decoded == sentinel_decoded_before && sentinel_rt == sentinel_rt_before &&
                               sentinel_rt2 == sentinel_rt2_before);

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
