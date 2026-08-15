#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/target.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/tests/compiler/link/link_test.h>

#if BUSTER_CPU_ARCH_X86_64
typedef struct X86CompletionCensusFormKey X86CompletionCensusFormKey;
struct X86CompletionCensusFormKey
{
    u32 form_id;
    u64 stable_hash;
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_sha_forms[] = {
    {8845, UINT64_C(0xee97de2b222473f0)}, {8846, UINT64_C(0x162cc7cd02282552)},
    {8847, UINT64_C(0xcccd57c10c8a3ebe)}, {8848, UINT64_C(0x447901e09888d774)},
    {8849, UINT64_C(0xa246c74e160d72be)}, {8850, UINT64_C(0x4b96b86a50fe74b4)},
    {8851, UINT64_C(0xfe0a75e19c857d33)}, {8852, UINT64_C(0xacf8ab20ea69dfd9)},
    {8853, UINT64_C(0xda318bc38fc8830c)}, {8854, UINT64_C(0xdce6b464e6d7e6fd)},
    {8855, UINT64_C(0x2dcf6f8ac01bf425)}, {8856, UINT64_C(0xec4e803f801b861e)},
    {8857, UINT64_C(0xf5b0cda333e330b9)}, {8858, UINT64_C(0x3ea943b7a373cef2)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_sha512_sm3_sm4_forms[] = {
    {8859, UINT64_C(0x01702a6139d65155)}, {8860, UINT64_C(0xa6c48cc98c24a04e)},
    {8861, UINT64_C(0xddb86a1207dfe71a)}, {8862, UINT64_C(0xe888ebbeaec57d39)},
    {8863, UINT64_C(0x8aea91c2f5558d91)}, {8864, UINT64_C(0x47665a25a6afd225)},
    {8865, UINT64_C(0x479b4a88d5f93781)}, {8866, UINT64_C(0xe433485b95f71dc4)},
    {8867, UINT64_C(0x65bcf8eafb7ca5d4)}, {8880, UINT64_C(0x6a7c8b4bef288adb)},
    {8881, UINT64_C(0x8d915dabec42201e)}, {8882, UINT64_C(0x016e4951e796afa1)},
    {8883, UINT64_C(0x884daebe93b9ad64)}, {8884, UINT64_C(0x9eb2af03c99ccead)},
    {8885, UINT64_C(0x4af438ea62b8ebf1)}, {8886, UINT64_C(0xf4d82a6998be5d9c)},
    {8887, UINT64_C(0xa821afa297be0ae4)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_sm4_evex_shadow_forms[] = {
    {8868, UINT64_C(0x4f413ab37e95c90c)}, {8869, UINT64_C(0x231f4438054baa67)},
    {8870, UINT64_C(0xedf48f4832166da7)}, {8871, UINT64_C(0x5d1167ea4a293887)},
    {8874, UINT64_C(0xfad5b4219e4c9f9e)}, {8875, UINT64_C(0x9d9f166debadb4bc)},
    {8876, UINT64_C(0x76d8bb1cef7791a2)}, {8877, UINT64_C(0xdfba78dcd184c2db)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_sm4_zmm_control_forms[] = {
    {8872, UINT64_C(0x93e9a6b4c6ed0df8)}, {8873, UINT64_C(0x4dea6ded6310143a)},
    {8878, UINT64_C(0xf66deb4d3f6722f8)}, {8879, UINT64_C(0x39328e28eeafa663)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_legacy_xmm_changed_forms[] = {
    {10085, UINT64_C(0xa188a812b928c5a8)}, {10103, UINT64_C(0x3d8fe741af12d499)},
    {10166, UINT64_C(0x7c707ec1ebf7b86f)}, {10183, UINT64_C(0x004a5906ba6c91f5)},
    {10211, UINT64_C(0xafaa14af00823f6a)}, {10213, UINT64_C(0x870cea79b97cd89f)},
    {10215, UINT64_C(0x2440ed8f2a4b7e6e)}, {10230, UINT64_C(0x93ca20607bdfb157)},
    {10232, UINT64_C(0xe66cdbe5695555df)}, {10234, UINT64_C(0x54484b5aa190cbf5)},
    {10418, UINT64_C(0xd7af09e34c35605e)}, {10445, UINT64_C(0x026f9a42fa8bc6aa)},
    {10488, UINT64_C(0x0c40051728589467)}, {10490, UINT64_C(0xbfc2a7138ba39c5c)},
    {10496, UINT64_C(0x5423a998b010043c)}, {10500, UINT64_C(0x3b28a14cb33b80bc)},
    {10504, UINT64_C(0x6cb1c8acaade2409)}, {10520, UINT64_C(0xd815e11e679ae999)},
    {10522, UINT64_C(0x0632007969649928)}, {10528, UINT64_C(0xa59e4601690c45f0)},
    {10532, UINT64_C(0x3ae9b095f672243b)}, {10536, UINT64_C(0xb573872e33ed2289)},
    {10580, UINT64_C(0x3022769a22e9cea5)}, {10777, UINT64_C(0x6cfe0b83efeb7bf4)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_state_forms[] = {
    {8019, UINT64_C(0x40e8f9af6ff5a76b)}, {8020, UINT64_C(0x7829643b5cb2d181)},
    {8021, UINT64_C(0x90f15af7e99df50a)}, {8022, UINT64_C(0x9e037a77c593480b)},
    {10970, UINT64_C(0x3e9fa6150563ddfc)}, {10972, UINT64_C(0xd5a85dc7155a0b69)},
    {10973, UINT64_C(0xfb0381953b38b296)}, {10974, UINT64_C(0x2f2bf66c5eae849c)},
    {10975, UINT64_C(0x81c2326e1590bea6)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_state_privileged_forms[] = {
    {10971, UINT64_C(0x74c3c5197ee56eef)}, {11009, UINT64_C(0x9211e7f7e752f5d6)},
    {11010, UINT64_C(0xfa1cb7c603e5dca2)}, {11011, UINT64_C(0x392090cc75f865d4)},
    {11012, UINT64_C(0xdd19095836024398)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_user_control_forms[] = {
    {8841, UINT64_C(0xe52c577bc4569794)},
    {8897, UINT64_C(0x82917ccf424112e3)}, {8898, UINT64_C(0x20ffc8f9046b2b1f)},
    {8899, UINT64_C(0x2e8cbc7a19d50754)}, {8900, UINT64_C(0x52f0554ffcb2e413)},
    {8901, UINT64_C(0x7a7de123f0ff5dec)},
    {9061, UINT64_C(0x9237af96c6c6d7de)}, {9062, UINT64_C(0xa29d21feaaa0f3a4)},
    {9063, UINT64_C(0xa51db3c3313ae1b2)},
};

typedef struct X86CompletionCensusOutcomeControl X86CompletionCensusOutcomeControl;
struct X86CompletionCensusOutcomeControl
{
    u32 form_id;
    u64 stable_hash;
    u8 intel_class;
    u8 att_class;
    u8 intel_reason;
    u8 att_reason;
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusOutcomeControl const x86_completion_census_legacy_xmm_controls[] = {
    // Store-direction forms remain the handwritten/source controls.
    {10087, UINT64_C(0x0fd91e57af10e912), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10105, UINT64_C(0x62f238e90101ab0c), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10420, UINT64_C(0x459319443976a607), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10447, UINT64_C(0x44495e911d41bbdc), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    // Ambiguous/unknown MOVSD/MOVQ forms do not acquire source authority.
    {10115, UINT64_C(0xc1d09809729f02cb), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION},
    {10116, UINT64_C(0xe883b23a096e675b), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION},
    {10581, UINT64_C(0x3283c18154507cf1), BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS},
    // Full metadata authority makes the opposite-dialect store forms exact in
    // both source syntaxes.
    {10089, UINT64_C(0xc7b0bcf068f01edf), BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10094, UINT64_C(0x1285dedcd0bd3f8d), BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10108, UINT64_C(0xb70fc1f33fd9be47), BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
    {10114, UINT64_C(0x9a647bd79a3b833d), BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT, BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE,
     BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_cache_forms[] = {
    {7979, UINT64_C(0x2c8e1ad84552d453)},
    {7980, UINT64_C(0x947cf4a7b1752469)},
    {9066, UINT64_C(0xe36fed6c3bd872c9)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_security_forms[] = {
    {1848, UINT64_C(0x6ea84c686eb80660)}, {8100, UINT64_C(0xda6c941b1c643db9)},
    {8101, UINT64_C(0x1f940b4f6cf73937)}, {8825, UINT64_C(0x58abbe28ba6ea168)},
    {8826, UINT64_C(0x75a52f773c0053aa)}, {8842, UINT64_C(0x95e29f75412146f1)},
    {8843, UINT64_C(0x2bec7e6265237549)}, {8844, UINT64_C(0x6c8e58373bbce760)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_shstk_forms[] = {
    // Public CPL3 shadow-stack forms become source-exact when SHSTK is in
    // the target profile; the APX WRSS aliases retain their alias class.
    {7938, UINT64_C(0xe2ec99c816c05831)}, {7939, UINT64_C(0xcf2bb2a4ffec71e4)},
    {7940, UINT64_C(0x7af16772f60b0cd8)}, {7941, UINT64_C(0xfb0ec2acc0cc67d6)},
    {7942, UINT64_C(0xf8e9c68beadf312e)}, {7943, UINT64_C(0x7f144158ebde8b45)},
    {7945, UINT64_C(0x625b38a34729634e)}, {7946, UINT64_C(0xd4f17e4479e73ef7)},
    {2839, UINT64_C(0xd3d36a7ca38fbe76)}, {2840, UINT64_C(0x5c33d83f492271b0)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_ptwrite_forms[] = {
    {8827, UINT64_C(0x263769210dc3d35f)}, {8828, UINT64_C(0x4371a45c4965fbc3)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_movdir64b_forms[] = {
    {1886, UINT64_C(0xcfdf1526a8946534)},
    {8762, UINT64_C(0x4f195bc17d9178e7)},
    {8763, UINT64_C(0xd4b2dade382c430e)},
};

BUSTER_GLOBAL_LOCAL X86CompletionCensusFormKey const x86_completion_census_enqcmd_forms[] = {
    {1749, UINT64_C(0xde770d6c9669345e)}, {1750, UINT64_C(0xa97e8fa2c0259e4e)},
    {8013, UINT64_C(0x9ca0bd82dff5ab9c)}, {8014, UINT64_C(0x313df55a468d4c4e)},
};
#endif

UnitTestResult x86_64_completion_census_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
#if BUSTER_CPU_ARCH_X86_64
    u32 form_count = buster_x86_metadata_form_count();
    u32 class_index = 0;
    u32 probe_index = 0;
    u32 probe_form_id = 0;
    u8 probe_bytes[16] = {0};
    BusterX86MetadataPhysicalOperand probe_operands[16] = {0};
    String8 probe_features[1] = {0};
    char8 probe_mnemonic[128] = {0};
    BusterX86MetadataPhysicalQuery probe_query = {0};
    BusterX86MetadataEmitResult probe_emit = {0};
    BusterX86CompletionCensusClass probe_class = BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED;
    BusterX86MetadataCoverageLedgerEntry* entries = arena_allocate(arguments->arena, BusterX86MetadataCoverageLedgerEntry, form_count);
    BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(entries, form_count);
    BusterX86CompletionCensusRecord* records = arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target census_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_DIAMOND_RAPIDS),
    };
    BusterX86CompletionCensusResult structural = buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena,
        .target = census_target,
        .records = records,
        .record_capacity = form_count,
        .structural_only = true,
    });
    BusterX86CompletionCensusResult aggregate = buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena,
        .target = census_target,
        .records = 0,
        .record_capacity = 0,
        .structural_only = true,
    });
    BUSTER_TEST(arguments, audit.complete);
    BUSTER_TEST(arguments, audit.entry_count == form_count);
    BUSTER_TEST(arguments, audit.normalized_entry_count == buster_x86_metadata_normalized_form_count());
    BUSTER_TEST(arguments, audit.emitted_count == 10607 && audit.blocked_count == 406);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_digest(entries, audit.entry_count, form_count) == UINT64_C(0xcbfbcf6712723dfa));
    BUSTER_TEST(arguments, structural.structural_complete);
    BUSTER_TEST(arguments, structural.records_complete);
    BUSTER_TEST(arguments, structural.form_partition_complete);
    BUSTER_TEST(arguments, structural.normalized_partition_complete);
    BUSTER_TEST(arguments, structural.metadata_partition_complete);
    BUSTER_TEST(arguments, structural.required_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_FORM_COUNT);
    BUSTER_TEST(arguments, structural.scanned_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_FORM_COUNT);
    BUSTER_TEST(arguments, structural.record_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_FORM_COUNT);
    BUSTER_TEST(arguments, structural.normalized_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NORMALIZED_COUNT);
    BUSTER_TEST(arguments, structural.non_normalized_form_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NON_NORMALIZED_COUNT);
    BUSTER_TEST(arguments, structural.metadata_emitted_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_EMITTED_COUNT);
    BUSTER_TEST(arguments, structural.metadata_blocked_count == BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_BLOCKED_COUNT);
    BUSTER_TEST(arguments, structural.metadata_emitted_count + structural.metadata_blocked_count == structural.normalized_form_count);
    BUSTER_TEST(arguments, structural.source_partition_expected_count == 0);
    BUSTER_TEST(arguments, structural.intel_source_partition_count == 0 && structural.att_source_partition_count == 0);
    BUSTER_TEST(arguments, structural.intel_attempted_count == 0 && structural.att_attempted_count == 0);
    BUSTER_TEST(arguments, structural.intel_all_passed && structural.att_all_passed);
    BUSTER_TEST(arguments, structural.diagnostic_count == 0 && structural.diagnostic_dropped_count == 0 && structural.diagnostics_complete);
    for (class_index = 0; class_index < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; class_index += 1)
    {
        BUSTER_TEST(arguments, structural.intel_class_counts[class_index] == 0);
        BUSTER_TEST(arguments, structural.att_class_counts[class_index] == 0);
    }
    BUSTER_TEST(arguments, aggregate.structural_complete && aggregate.records_complete);
    BUSTER_TEST(arguments, aggregate.record_count == 0);
    BUSTER_TEST(arguments, aggregate.form_partition_complete && aggregate.normalized_partition_complete && aggregate.metadata_partition_complete);
    BUSTER_TEST(arguments, aggregate.source_partition_expected_count == 0);
    BUSTER_TEST(arguments, aggregate.intel_source_partition_count == 0 && aggregate.att_source_partition_count == 0);
    BUSTER_TEST(arguments, aggregate.intel_attempted_count == 0 && aggregate.att_attempted_count == 0);
    BUSTER_TEST(arguments, aggregate.diagnostic_count == 0 && aggregate.diagnostic_dropped_count == 0 && aggregate.diagnostics_complete);
    for (class_index = 0; class_index < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; class_index += 1)
    {
        BUSTER_TEST(arguments, aggregate.intel_class_counts[class_index] == 0);
        BUSTER_TEST(arguments, aggregate.att_class_counts[class_index] == 0);
    }
    BUSTER_TEST(arguments, aggregate.metadata_emitted_count == structural.metadata_emitted_count &&
                             aggregate.metadata_blocked_count == structural.metadata_blocked_count);

    // Source reachability is intentionally measured separately from the
    // structural pass.  These counts are the current exhaustive ledger; the
    // reason buckets are assigned only at concrete source-builder/parser
    // branches, never inferred from a form's incidental shape.
    BusterX86CompletionCensusResult source = buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena,
        .target = census_target,
        .records = records,
        .record_capacity = form_count,
        .run_intel = true,
        .run_att = true,
    });
    u8 sha_census_inventory_text[14 * 32] = {0};
    u32 sha_census_inventory_length = 0;
    u32 sha_census_intel_exact_count = 0;
    u32 sha_census_att_exact_count = 0;
    for (u32 sha_census_index = 0; sha_census_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sha_forms);
         sha_census_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_sha_forms[sha_census_index];
        BusterX86MetadataForm sha_form = {0};
        BusterX86MetadataFormKey sha_key = {0};
        String8 sha_line = {0};
        bool sha_form_ok = buster_x86_metadata_form(expected->form_id, &sha_form) &&
                           buster_x86_metadata_form_key(expected->form_id, &sha_key);
        BUSTER_TEST(arguments, sha_form_ok && sha_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(sha_form.isa_set), S8("SHA")));
        sha_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"), expected->form_id,
                                 expected->stable_hash);
        BUSTER_TEST(arguments, sha_census_inventory_length + sha_line.length <= sizeof(sha_census_inventory_text));
        if (sha_census_inventory_length + sha_line.length <= sizeof(sha_census_inventory_text))
        {
            memcpy(sha_census_inventory_text + sha_census_inventory_length, sha_line.pointer, sha_line.length);
            sha_census_inventory_length += (u32)sha_line.length;
        }
        BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                 records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
        sha_census_intel_exact_count += records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                        records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE;
        sha_census_att_exact_count += records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                      records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE;
    }
    u8 sha_census_inventory_digest[32] = {0};
    static u8 const expected_sha_census_inventory_digest[32] = {
        0xcf, 0x0c, 0x39, 0xef, 0x0e, 0x44, 0x53, 0x9a, 0x8e, 0x3a, 0xfa, 0x8d, 0xb3, 0x05, 0x0d, 0x99,
        0xed, 0x6c, 0x18, 0x08, 0x18, 0x62, 0xae, 0x07, 0xe2, 0x0d, 0x95, 0x19, 0x4f, 0x6a, 0xc3, 0xee,
    };
    link_sha256(arguments->arena, sha_census_inventory_text, sha_census_inventory_length, sha_census_inventory_digest);
    BUSTER_TEST(arguments, sha_census_inventory_length == 308 && sha_census_intel_exact_count == 14 &&
                             sha_census_att_exact_count == 14 &&
                             memcmp(sha_census_inventory_digest, expected_sha_census_inventory_digest,
                                    sizeof(expected_sha_census_inventory_digest)) == 0);
    u8 crypto_census_inventory_text[17 * 32] = {0};
    u32 crypto_census_inventory_length = 0;
    u32 crypto_census_intel_exact_count = 0;
    u32 crypto_census_att_exact_count = 0;
    for (u32 crypto_census_index = 0;
         crypto_census_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sha512_sm3_sm4_forms);
         crypto_census_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_sha512_sm3_sm4_forms[crypto_census_index];
        BusterX86MetadataForm crypto_form = {0};
        BusterX86MetadataFormKey crypto_key = {0};
        bool crypto_form_ok = buster_x86_metadata_form(expected->form_id, &crypto_form) &&
                              buster_x86_metadata_form_key(expected->form_id, &crypto_key);
        bool is_sha512 = expected->form_id >= 8859 && expected->form_id <= 8861;
        bool is_sm3 = expected->form_id >= 8862 && expected->form_id <= 8867;
        String8 expected_isa = is_sha512 ? S8("SHA512") : (is_sm3 ? S8("SM3") : S8("SM4"));
        BUSTER_TEST(arguments, crypto_form_ok && crypto_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(crypto_form.isa_set), expected_isa));
        String8 crypto_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                             expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, crypto_census_inventory_length + crypto_line.length <= sizeof(crypto_census_inventory_text));
        if (crypto_census_inventory_length + crypto_line.length <= sizeof(crypto_census_inventory_text))
        {
            memcpy(crypto_census_inventory_text + crypto_census_inventory_length, crypto_line.pointer, crypto_line.length);
            crypto_census_inventory_length += (u32)crypto_line.length;
        }
        BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                 records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
        crypto_census_intel_exact_count += records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
        crypto_census_att_exact_count += records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
    }
    u8 crypto_census_inventory_digest[32] = {0};
    static u8 const expected_crypto_census_inventory_digest[32] = {
        0x98, 0x28, 0x8d, 0x16, 0xec, 0x90, 0x08, 0x97, 0x04, 0xb3, 0x54, 0x79, 0xe9, 0x26, 0x1b, 0x57,
        0xf3, 0x1b, 0xb6, 0x20, 0xf7, 0x41, 0xd7, 0xd7, 0x9c, 0x29, 0x08, 0xfe, 0x32, 0x2b, 0x51, 0x89,
    };
    link_sha256(arguments->arena, crypto_census_inventory_text, crypto_census_inventory_length, crypto_census_inventory_digest);
    BUSTER_TEST(arguments, crypto_census_inventory_length == 374 && crypto_census_intel_exact_count == 17 &&
                             crypto_census_att_exact_count == 17 &&
                             memcmp(crypto_census_inventory_digest, expected_crypto_census_inventory_digest,
                                    sizeof(expected_crypto_census_inventory_digest)) == 0);
    u8 state_census_inventory_text[9 * 32] = {0};
    u32 state_census_inventory_length = 0;
    u32 state_census_intel_exact_count = 0;
    u32 state_census_att_exact_count = 0;
    for (u32 state_census_index = 0;
         state_census_index < BUSTER_ARRAY_LENGTH(x86_completion_census_state_forms);
         state_census_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_state_forms[state_census_index];
        BusterX86MetadataForm state_form = {0};
        BusterX86MetadataFormKey state_key = {0};
        bool state_form_ok = buster_x86_metadata_form(expected->form_id, &state_form) &&
                             buster_x86_metadata_form_key(expected->form_id, &state_key);
        BUSTER_TEST(arguments, state_form_ok && state_key.stable_hash == expected->stable_hash &&
                                 buster_x86_metadata_string_span(state_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(state_form.cpl, 0) == '3');
        String8 state_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                            expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, state_census_inventory_length + state_line.length <= sizeof(state_census_inventory_text));
        if (state_census_inventory_length + state_line.length <= sizeof(state_census_inventory_text))
        {
            memcpy(state_census_inventory_text + state_census_inventory_length, state_line.pointer, state_line.length);
            state_census_inventory_length += (u32)state_line.length;
        }
        BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                 records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
        state_census_intel_exact_count += records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
        state_census_att_exact_count += records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
    }
    u8 state_census_inventory_digest[32] = {0};
    link_sha256(arguments->arena, state_census_inventory_text, state_census_inventory_length, state_census_inventory_digest);
    static u8 const expected_state_census_inventory_digest[32] = {
        0xf9, 0xa6, 0xa7, 0xc1, 0x31, 0x5d, 0xaf, 0x67,
        0x42, 0xdd, 0x4e, 0x84, 0xa4, 0xeb, 0x7d, 0xea,
        0x0f, 0x7f, 0xb9, 0x67, 0xae, 0x3d, 0xac, 0x55,
        0xfa, 0xc6, 0xf4, 0xe1, 0x06, 0x82, 0xbf, 0xe7,
    };
    BUSTER_TEST(arguments, state_census_inventory_length == 203);
    BUSTER_TEST(arguments, state_census_intel_exact_count == 9);
    BUSTER_TEST(arguments, state_census_att_exact_count == 9);
    BUSTER_TEST(arguments, memcmp(state_census_inventory_digest, expected_state_census_inventory_digest,
                                  sizeof(expected_state_census_inventory_digest)) == 0);
    for (u32 state_privileged_index = 0;
         state_privileged_index < BUSTER_ARRAY_LENGTH(x86_completion_census_state_privileged_forms);
         state_privileged_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_state_privileged_forms[state_privileged_index];
        BusterX86MetadataForm state_form = {0};
        BusterX86MetadataFormKey state_key = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(expected->form_id, &state_form) &&
                                 buster_x86_metadata_form_key(expected->form_id, &state_key) &&
                                 buster_x86_metadata_string_span(state_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(state_form.cpl, 0) == '0' &&
                                 state_key.stable_hash == expected->stable_hash &&
                                 records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    BusterX86CompletionCensusRecord* state_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target state_baseline_target = census_target;
    state_baseline_target.cpu_features = target_cpu_features_remove(state_baseline_target.cpu_features,
                                                                     TARGET_CPU_FEATURE_X86_FSGSBASE);
    state_baseline_target.cpu_features = target_cpu_features_remove(state_baseline_target.cpu_features,
                                                                     TARGET_CPU_FEATURE_X86_XSAVE);
    state_baseline_target.cpu_features = target_cpu_features_remove(state_baseline_target.cpu_features,
                                                                     TARGET_CPU_FEATURE_X86_XSAVES);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = state_baseline_target, .records = state_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 state_changed_count = 0;
    for (u32 state_compare_form_id = 0; state_compare_form_id < form_count; state_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &state_baseline_records[state_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[state_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count || before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        state_changed_count += 1;
        bool state_form = false;
        for (u32 state_index = 0; state_index < BUSTER_ARRAY_LENGTH(x86_completion_census_state_forms); state_index += 1)
        {
            state_form |= state_compare_form_id == x86_completion_census_state_forms[state_index].form_id;
        }
        BUSTER_TEST(arguments, state_form);
    }
    for (u32 state_index = 0; state_index < BUSTER_ARRAY_LENGTH(x86_completion_census_state_forms); state_index += 1)
    {
        u32 state_form_id = x86_completion_census_state_forms[state_index].form_id;
        BusterX86CompletionCensusRecord const* before = &state_baseline_records[state_form_id];
        BusterX86CompletionCensusRecord const* after = &records[state_form_id];
        BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    BUSTER_TEST(arguments, state_changed_count == BUSTER_ARRAY_LENGTH(x86_completion_census_state_forms));

    // The unprivileged control/scheduling source movement is pinned by the
    // nine CPL3 forms below.  The feature-disabled comparison must show no
    // other form moving, including the existing privileged state controls.
    u8 user_control_inventory_text[9 * 32] = {0};
    u32 user_control_inventory_length = 0;
    u32 user_control_intel_exact_count = 0;
    u32 user_control_att_exact_count = 0;
    static String8 const user_control_isa_sets[] = {
        S8_INITIALIZER("SERIALIZE"),
        S8_INITIALIZER("UINTR"), S8_INITIALIZER("UINTR"), S8_INITIALIZER("UINTR"),
        S8_INITIALIZER("UINTR"), S8_INITIALIZER("UINTR"),
        S8_INITIALIZER("WAITPKG"), S8_INITIALIZER("WAITPKG"), S8_INITIALIZER("WAITPKG"),
    };
    for (u32 user_control_index = 0;
         user_control_index < BUSTER_ARRAY_LENGTH(x86_completion_census_user_control_forms);
         user_control_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_user_control_forms[user_control_index];
        BusterX86MetadataForm user_control_form = {0};
        BusterX86MetadataFormKey user_control_key = {0};
        bool user_control_form_ok = buster_x86_metadata_form(expected->form_id, &user_control_form) &&
                                    buster_x86_metadata_form_key(expected->form_id, &user_control_key);
        BUSTER_TEST(arguments, user_control_form_ok && user_control_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(user_control_form.isa_set),
                                              user_control_isa_sets[user_control_index]) &&
                                 buster_x86_metadata_string_span(user_control_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(user_control_form.cpl, 0) == '3');
        String8 user_control_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                  expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, user_control_inventory_length + user_control_line.length <= sizeof(user_control_inventory_text));
        if (user_control_inventory_length + user_control_line.length <= sizeof(user_control_inventory_text))
        {
            memcpy(user_control_inventory_text + user_control_inventory_length, user_control_line.pointer,
                   user_control_line.length);
            user_control_inventory_length += (u32)user_control_line.length;
        }
        BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                 records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
        user_control_intel_exact_count += records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
        user_control_att_exact_count += records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
    }
    u8 user_control_inventory_digest[32] = {0};
    static u8 const expected_user_control_inventory_digest[32] = {
        0x93, 0x97, 0x70, 0xc8, 0x45, 0xd2, 0xb7, 0x26,
        0x61, 0xb7, 0x01, 0x7d, 0x9c, 0xc0, 0xe9, 0x4e,
        0x7d, 0x02, 0xd8, 0x50, 0x54, 0x65, 0x23, 0xd3,
        0x3f, 0x35, 0xb6, 0x0d, 0xba, 0xd1, 0x25, 0x47,
    };
    link_sha256(arguments->arena, user_control_inventory_text, user_control_inventory_length, user_control_inventory_digest);
    BUSTER_TEST(arguments, user_control_inventory_length == 198 && user_control_intel_exact_count == 9 &&
                             user_control_att_exact_count == 9 &&
                             memcmp(user_control_inventory_digest, expected_user_control_inventory_digest,
                                    sizeof(expected_user_control_inventory_digest)) == 0);
    BusterX86CompletionCensusRecord* user_control_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target user_control_baseline_target = census_target;
    user_control_baseline_target.cpu_features = target_cpu_features_remove(user_control_baseline_target.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_SERIALIZE);
    user_control_baseline_target.cpu_features = target_cpu_features_remove(user_control_baseline_target.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_WAITPKG);
    user_control_baseline_target.cpu_features = target_cpu_features_remove(user_control_baseline_target.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_UINTR);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = user_control_baseline_target, .records = user_control_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 user_control_changed_count = 0;
    for (u32 user_control_compare_form_id = 0; user_control_compare_form_id < form_count;
         user_control_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &user_control_baseline_records[user_control_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[user_control_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        user_control_changed_count += 1;
        bool selected = false;
        for (u32 user_control_index = 0;
             user_control_index < BUSTER_ARRAY_LENGTH(x86_completion_census_user_control_forms);
             user_control_index += 1)
        {
            selected |= user_control_compare_form_id == x86_completion_census_user_control_forms[user_control_index].form_id;
        }
        BUSTER_TEST(arguments, selected);
    }
    for (u32 user_control_index = 0;
         user_control_index < BUSTER_ARRAY_LENGTH(x86_completion_census_user_control_forms);
         user_control_index += 1)
    {
        u32 user_control_form_id = x86_completion_census_user_control_forms[user_control_index].form_id;
        BusterX86CompletionCensusRecord const* before = &user_control_baseline_records[user_control_form_id];
        BusterX86CompletionCensusRecord const* after = &records[user_control_form_id];
        BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    BUSTER_TEST(arguments, user_control_changed_count == BUSTER_ARRAY_LENGTH(x86_completion_census_user_control_forms));
    for (u32 state_privileged_index = 0;
         state_privileged_index < BUSTER_ARRAY_LENGTH(x86_completion_census_state_privileged_forms);
         state_privileged_index += 1)
    {
        u32 state_privileged_form_id = x86_completion_census_state_privileged_forms[state_privileged_index].form_id;
        BusterX86CompletionCensusRecord const* before = &user_control_baseline_records[state_privileged_form_id];
        BusterX86CompletionCensusRecord const* after = &records[state_privileged_form_id];
        BUSTER_TEST(arguments, before->intel_class == after->intel_class && before->att_class == after->att_class &&
                                 before->intel_source_reason == after->intel_source_reason &&
                                 before->att_source_reason == after->att_source_reason);
    }

    // Cache-maintenance source reachability is pinned by the two CPL3 forms;
    // the CPL0 WBNOINVD form remains outside the source partition.  The
    // feature-disabled comparison must show exactly the two public rows moving.
    u8 cache_inventory_text[3 * 32] = {0};
    u32 cache_inventory_length = 0;
    static String8 const cache_isa_sets[] = {
        S8_INITIALIZER("CLFLUSHOPT"), S8_INITIALIZER("CLWB"), S8_INITIALIZER("WBNOINVD"),
    };
    static char8 const cache_cpls[] = {'3', '3', '0'};
    for (u32 cache_index = 0; cache_index < BUSTER_ARRAY_LENGTH(x86_completion_census_cache_forms); cache_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_cache_forms[cache_index];
        BusterX86MetadataForm cache_form = {0};
        BusterX86MetadataFormKey cache_key = {0};
        bool cache_form_ok = buster_x86_metadata_form(expected->form_id, &cache_form) &&
                             buster_x86_metadata_form_key(expected->form_id, &cache_key);
        BUSTER_TEST(arguments, cache_form_ok && cache_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(cache_form.isa_set), cache_isa_sets[cache_index]) &&
                                 buster_x86_metadata_string_span(cache_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(cache_form.cpl, 0) == cache_cpls[cache_index]);
        String8 cache_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                            expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, cache_inventory_length + cache_line.length <= sizeof(cache_inventory_text));
        if (cache_inventory_length + cache_line.length <= sizeof(cache_inventory_text))
        {
            memcpy(cache_inventory_text + cache_inventory_length, cache_line.pointer, cache_line.length);
            cache_inventory_length += (u32)cache_line.length;
        }
        if (cache_index < 2)
        {
            BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                     records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                     records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                     records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
        }
        else
        {
            BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                     records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                     records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        }
    }
    u8 cache_inventory_digest[32] = {0};
    static u8 const expected_cache_inventory_digest[32] = {
        0x9b, 0x47, 0x66, 0xc8, 0xb0, 0xa4, 0x6e, 0x26,
        0xca, 0x20, 0x1c, 0xf3, 0x2e, 0x3b, 0x72, 0x27,
        0xf4, 0x83, 0x57, 0xf6, 0xa8, 0x9a, 0x5b, 0xc8,
        0x16, 0x37, 0x0b, 0x56, 0x7a, 0xc0, 0x63, 0x8c,
    };
    link_sha256(arguments->arena, cache_inventory_text, cache_inventory_length, cache_inventory_digest);
    BUSTER_TEST(arguments, cache_inventory_length == 66 &&
                             memcmp(cache_inventory_digest, expected_cache_inventory_digest,
                                    sizeof(expected_cache_inventory_digest)) == 0);
    BusterX86CompletionCensusRecord* cache_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target cache_baseline_target = census_target;
    cache_baseline_target.cpu_features = target_cpu_features_remove(cache_baseline_target.cpu_features,
                                                                    TARGET_CPU_FEATURE_X86_CLFLUSHOPT);
    cache_baseline_target.cpu_features = target_cpu_features_remove(cache_baseline_target.cpu_features,
                                                                    TARGET_CPU_FEATURE_X86_CLWB);
    cache_baseline_target.cpu_features = target_cpu_features_remove(cache_baseline_target.cpu_features,
                                                                    TARGET_CPU_FEATURE_X86_WBNOINVD);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = cache_baseline_target, .records = cache_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 cache_changed_count = 0;
    for (u32 cache_compare_form_id = 0; cache_compare_form_id < form_count; cache_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &cache_baseline_records[cache_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[cache_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        cache_changed_count += 1;
        BUSTER_TEST(arguments, cache_compare_form_id == x86_completion_census_cache_forms[0].form_id ||
                                 cache_compare_form_id == x86_completion_census_cache_forms[1].form_id);
    }
    for (u32 cache_index = 0; cache_index < 2; cache_index += 1)
    {
        u32 cache_form_id = x86_completion_census_cache_forms[cache_index].form_id;
        BusterX86CompletionCensusRecord const* before = &cache_baseline_records[cache_form_id];
        BusterX86CompletionCensusRecord const* after = &records[cache_form_id];
        BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    u32 cache_privileged_form_id = x86_completion_census_cache_forms[2].form_id;
    BUSTER_TEST(arguments, cache_baseline_records[cache_privileged_form_id].intel_class ==
                             records[cache_privileged_form_id].intel_class &&
                             cache_baseline_records[cache_privileged_form_id].att_class ==
                             records[cache_privileged_form_id].att_class &&
                             cache_baseline_records[cache_privileged_form_id].intel_source_reason ==
                             records[cache_privileged_form_id].intel_source_reason &&
                             cache_baseline_records[cache_privileged_form_id].att_source_reason ==
                             records[cache_privileged_form_id].att_source_reason);
    BUSTER_TEST(arguments, cache_changed_count == 2);

    // Security/isolation source reachability is pinned by two public PKU and
    // two public SGX forms. INVPCID and SGX's ring-0 form remain outside the
    // source partition; the feature-disabled comparison must show exactly
    // the four public rows moving.
    u8 security_inventory_text[8 * 32] = {0};
    u32 security_inventory_length = 0;
    static String8 const security_isa_sets[] = {
        S8_INITIALIZER("APX_F_INVPCID"), S8_INITIALIZER("INVPCID"), S8_INITIALIZER("INVPCID"),
        S8_INITIALIZER("PKU"), S8_INITIALIZER("PKU"), S8_INITIALIZER("SGX_ENCLV"),
        S8_INITIALIZER("SGX"), S8_INITIALIZER("SGX"),
    };
    static char8 const security_cpls[] = {'0', '0', '0', '3', '3', '3', '3', '0'};
    for (u32 security_index = 0; security_index < BUSTER_ARRAY_LENGTH(x86_completion_census_security_forms); security_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_security_forms[security_index];
        BusterX86MetadataForm security_form = {0};
        BusterX86MetadataFormKey security_key = {0};
        bool security_form_ok = buster_x86_metadata_form(expected->form_id, &security_form) &&
                                buster_x86_metadata_form_key(expected->form_id, &security_key);
        BUSTER_TEST(arguments, security_form_ok && security_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(security_form.isa_set), security_isa_sets[security_index]) &&
                                 buster_x86_metadata_string_span(security_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(security_form.cpl, 0) == security_cpls[security_index]);
        String8 security_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                               expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, security_inventory_length + security_line.length <= sizeof(security_inventory_text));
        if (security_inventory_length + security_line.length <= sizeof(security_inventory_text))
        {
            memcpy(security_inventory_text + security_inventory_length, security_line.pointer, security_line.length);
            security_inventory_length += (u32)security_line.length;
        }
        bool security_public = security_index >= 3 && security_index <= 6;
        if (security_public)
        {
            BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                     records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count);
        }
        else
        {
            BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                     records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                     records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        }
    }
    u8 security_inventory_digest[32] = {0};
    static u8 const expected_security_inventory_digest[32] = {
        0x97, 0xfb, 0x21, 0xcb, 0x9b, 0x6b, 0x7b, 0x94,
        0xb9, 0x0b, 0xa0, 0x22, 0xc6, 0x6f, 0x1d, 0x53,
        0xd5, 0x50, 0x33, 0x21, 0x90, 0x44, 0x57, 0xf8,
        0xde, 0xb5, 0x74, 0x11, 0xc1, 0xa1, 0x53, 0x8f,
    };
    link_sha256(arguments->arena, security_inventory_text, security_inventory_length, security_inventory_digest);
    BUSTER_TEST(arguments, security_inventory_length == 176 &&
                             memcmp(security_inventory_digest, expected_security_inventory_digest,
                                    sizeof(expected_security_inventory_digest)) == 0);
    BusterX86CompletionCensusRecord* security_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target security_baseline_target = census_target;
    security_baseline_target.cpu_features = target_cpu_features_remove(security_baseline_target.cpu_features,
                                                                        TARGET_CPU_FEATURE_X86_INVPCID);
    security_baseline_target.cpu_features = target_cpu_features_remove(security_baseline_target.cpu_features,
                                                                        TARGET_CPU_FEATURE_X86_PKU);
    security_baseline_target.cpu_features = target_cpu_features_remove(security_baseline_target.cpu_features,
                                                                        TARGET_CPU_FEATURE_X86_SGX);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = security_baseline_target, .records = security_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 security_changed_count = 0;
    for (u32 security_compare_form_id = 0; security_compare_form_id < form_count; security_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &security_baseline_records[security_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[security_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        security_changed_count += 1;
        bool security_public = false;
        for (u32 security_index = 3; security_index <= 6; security_index += 1)
        {
            security_public |= security_compare_form_id == x86_completion_census_security_forms[security_index].form_id;
        }
        BUSTER_TEST(arguments, security_public);
    }
    for (u32 security_index = 0; security_index < BUSTER_ARRAY_LENGTH(x86_completion_census_security_forms); security_index += 1)
    {
        u32 security_form_id = x86_completion_census_security_forms[security_index].form_id;
        BusterX86CompletionCensusRecord const* before = &security_baseline_records[security_form_id];
        BusterX86CompletionCensusRecord const* after = &records[security_form_id];
        bool security_public = security_index >= 3 && security_index <= 6;
        if (security_public)
        {
            BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                     before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                     before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                     before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                     after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        }
        else
        {
            BUSTER_TEST(arguments, before->intel_class == after->intel_class && before->att_class == after->att_class &&
                                     before->intel_source_reason == after->intel_source_reason &&
                                     before->att_source_reason == after->att_source_reason &&
                                     after->intel_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED &&
                                     after->att_class == BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED);
        }
    }
    BUSTER_TEST(arguments, security_changed_count == 4);

    // SHSTK/CET source reachability is pinned to the eight public CET rows
    // plus the two APX WRSS aliases.  ENDBR is IBT-only, while the ring-0
    // shadow-stack controls remain outside this CPL3 source partition.
    static String8 const shstk_isa_sets[] = {
        S8_INITIALIZER("CET"), S8_INITIALIZER("CET"), S8_INITIALIZER("CET"), S8_INITIALIZER("CET"),
        S8_INITIALIZER("CET"), S8_INITIALIZER("CET"), S8_INITIALIZER("CET"), S8_INITIALIZER("CET"),
        S8_INITIALIZER("APX_F_CET"), S8_INITIALIZER("APX_F_CET"),
    };
    static String8 const shstk_iclasses[] = {
        S8_INITIALIZER("INCSSPD"), S8_INITIALIZER("INCSSPQ"), S8_INITIALIZER("RDSSPD"),
        S8_INITIALIZER("RDSSPQ"), S8_INITIALIZER("RSTORSSP"), S8_INITIALIZER("SAVEPREVSSP"),
        S8_INITIALIZER("WRSSD"), S8_INITIALIZER("WRSSQ"), S8_INITIALIZER("WRSSD"), S8_INITIALIZER("WRSSQ"),
    };
    u8 shstk_inventory_text[10 * 32] = {0};
    u32 shstk_inventory_length = 0;
    for (u32 shstk_index = 0; shstk_index < BUSTER_ARRAY_LENGTH(x86_completion_census_shstk_forms); shstk_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_shstk_forms[shstk_index];
        BusterX86MetadataForm shstk_form = {0};
        BusterX86MetadataFormKey shstk_key = {0};
        bool shstk_form_ok = buster_x86_metadata_form(expected->form_id, &shstk_form) &&
                             buster_x86_metadata_form_key(expected->form_id, &shstk_key);
        BUSTER_TEST(arguments, shstk_form_ok && shstk_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(shstk_form.isa_set), shstk_isa_sets[shstk_index]) &&
                                 string_equal(buster_x86_metadata_string_span(shstk_form.iclass), shstk_iclasses[shstk_index]) &&
                                 buster_x86_metadata_string_span(shstk_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(shstk_form.cpl, 0) == '3');
        String8 shstk_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                            expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, shstk_inventory_length + shstk_line.length <= sizeof(shstk_inventory_text));
        if (shstk_inventory_length + shstk_line.length <= sizeof(shstk_inventory_text))
        {
            memcpy(shstk_inventory_text + shstk_inventory_length, shstk_line.pointer, shstk_line.length);
            shstk_inventory_length += (u32)shstk_line.length;
        }
        bool shstk_alias = shstk_index >= 8;
        BUSTER_TEST(arguments, records[expected->form_id].intel_class ==
                                     (shstk_alias ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT :
                                                    BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT) &&
                                 records[expected->form_id].att_class ==
                                     (shstk_alias ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT :
                                                    BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT) &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    u8 shstk_inventory_digest[32] = {0};
    static u8 const expected_shstk_inventory_digest[32] = {
        0xc9, 0x8a, 0x9f, 0x9c, 0x7f, 0x9b, 0xfd, 0x67,
        0x09, 0x0b, 0x05, 0x7a, 0x0d, 0x25, 0x28, 0x6f,
        0x2b, 0x9d, 0xaf, 0x74, 0x13, 0x3f, 0x5e, 0xb2,
        0x3d, 0x45, 0x5a, 0xda, 0xba, 0x26, 0xfc, 0xe4,
    };
    link_sha256(arguments->arena, shstk_inventory_text, shstk_inventory_length, shstk_inventory_digest);
    BUSTER_TEST(arguments, shstk_inventory_length == 220 &&
                             memcmp(shstk_inventory_digest, expected_shstk_inventory_digest,
                                    sizeof(expected_shstk_inventory_digest)) == 0);
    BusterX86CompletionCensusRecord* shstk_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target shstk_baseline_target = census_target;
    shstk_baseline_target.cpu_features = target_cpu_features_remove(shstk_baseline_target.cpu_features,
                                                                      TARGET_CPU_FEATURE_X86_SHSTK);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = shstk_baseline_target, .records = shstk_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 shstk_changed_count = 0;
    for (u32 shstk_compare_form_id = 0; shstk_compare_form_id < form_count; shstk_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &shstk_baseline_records[shstk_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[shstk_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        shstk_changed_count += 1;
        bool selected = false;
        for (u32 shstk_index = 0; shstk_index < BUSTER_ARRAY_LENGTH(x86_completion_census_shstk_forms); shstk_index += 1)
            selected |= shstk_compare_form_id == x86_completion_census_shstk_forms[shstk_index].form_id;
        BUSTER_TEST(arguments, selected);
    }
    for (u32 shstk_index = 0; shstk_index < BUSTER_ARRAY_LENGTH(x86_completion_census_shstk_forms); shstk_index += 1)
    {
        u32 shstk_form_id = x86_completion_census_shstk_forms[shstk_index].form_id;
        BusterX86CompletionCensusRecord const* before = &shstk_baseline_records[shstk_form_id];
        BusterX86CompletionCensusRecord const* after = &records[shstk_form_id];
        bool shstk_alias = shstk_index >= 8;
        BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after->intel_class ==
                                     (shstk_alias ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT :
                                                    BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT) &&
                                 after->att_class ==
                                     (shstk_alias ? BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT :
                                                    BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT) &&
                                 after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    static u32 const shstk_control_form_ids[] = {7935, 7936, 7937, 7944, 7947, 7948};
    for (u32 control_index = 0; control_index < BUSTER_ARRAY_LENGTH(shstk_control_form_ids); control_index += 1)
    {
        u32 control_form_id = shstk_control_form_ids[control_index];
        BUSTER_TEST(arguments, shstk_baseline_records[control_form_id].intel_class == records[control_form_id].intel_class &&
                                 shstk_baseline_records[control_form_id].att_class == records[control_form_id].att_class &&
                                 shstk_baseline_records[control_form_id].intel_source_reason == records[control_form_id].intel_source_reason &&
                                 shstk_baseline_records[control_form_id].att_source_reason == records[control_form_id].att_source_reason);
    }
    BUSTER_TEST(arguments, shstk_changed_count == BUSTER_ARRAY_LENGTH(x86_completion_census_shstk_forms));

    // PTWRITE has two public CPL3 forms.  The feature-disabled comparison
    // proves that model-default exposure moves exactly these rows and that
    // the Intel and AT&T source paths agree on their bytes.
    u8 ptwrite_inventory_text[2 * 32] = {0};
    u32 ptwrite_inventory_length = 0;
    for (u32 ptwrite_index = 0; ptwrite_index < BUSTER_ARRAY_LENGTH(x86_completion_census_ptwrite_forms);
         ptwrite_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_ptwrite_forms[ptwrite_index];
        BusterX86MetadataForm ptwrite_form = {0};
        BusterX86MetadataFormKey ptwrite_key = {0};
        bool ptwrite_form_ok = buster_x86_metadata_form(expected->form_id, &ptwrite_form) &&
                               buster_x86_metadata_form_key(expected->form_id, &ptwrite_key);
        BUSTER_TEST(arguments, ptwrite_form_ok && ptwrite_key.stable_hash == expected->stable_hash &&
                                 string_equal(buster_x86_metadata_string_span(ptwrite_form.isa_set), S8("PTWRITE")) &&
                                 buster_x86_metadata_string_span(ptwrite_form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(ptwrite_form.cpl, 0) == '3');
        String8 ptwrite_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                             expected->form_id, expected->stable_hash);
        BUSTER_TEST(arguments, ptwrite_inventory_length + ptwrite_line.length <= sizeof(ptwrite_inventory_text));
        if (ptwrite_inventory_length + ptwrite_line.length <= sizeof(ptwrite_inventory_text))
        {
            memcpy(ptwrite_inventory_text + ptwrite_inventory_length, ptwrite_line.pointer, ptwrite_line.length);
            ptwrite_inventory_length += (u32)ptwrite_line.length;
        }
        BUSTER_TEST(arguments, records[expected->form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected->form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected->form_id].intel_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].att_byte_count == records[expected->form_id].metadata_byte_count &&
                                 records[expected->form_id].intel_relocation_count == records[expected->form_id].metadata_relocation_count &&
                                 records[expected->form_id].att_relocation_count == records[expected->form_id].metadata_relocation_count);
    }
    u8 ptwrite_inventory_digest[32] = {0};
    static u8 const expected_ptwrite_inventory_digest[32] = {
        0xc1, 0x64, 0xdb, 0x81, 0x72, 0x27, 0xc0, 0x5f,
        0x5b, 0xcf, 0x27, 0xdc, 0x77, 0x7d, 0x79, 0x3c,
        0x93, 0xc5, 0xb9, 0xc5, 0x13, 0xb2, 0x5e, 0x1d,
        0x1a, 0xe8, 0x0f, 0xfb, 0x7d, 0xf5, 0xaf, 0x39,
    };
    link_sha256(arguments->arena, ptwrite_inventory_text, ptwrite_inventory_length, ptwrite_inventory_digest);
    BUSTER_TEST(arguments, ptwrite_inventory_length == 44 &&
                             memcmp(ptwrite_inventory_digest, expected_ptwrite_inventory_digest,
                                    sizeof(expected_ptwrite_inventory_digest)) == 0);
    BusterX86CompletionCensusRecord* ptwrite_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target ptwrite_baseline_target = census_target;
    ptwrite_baseline_target.cpu_features = target_cpu_features_remove(ptwrite_baseline_target.cpu_features,
                                                                       TARGET_CPU_FEATURE_X86_PTWRITE);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = ptwrite_baseline_target, .records = ptwrite_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 ptwrite_changed_count = 0;
    for (u32 ptwrite_compare_form_id = 0; ptwrite_compare_form_id < form_count; ptwrite_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &ptwrite_baseline_records[ptwrite_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[ptwrite_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        ptwrite_changed_count += 1;
        bool selected = false;
        for (u32 ptwrite_index = 0; ptwrite_index < BUSTER_ARRAY_LENGTH(x86_completion_census_ptwrite_forms);
             ptwrite_index += 1)
            selected |= ptwrite_compare_form_id == x86_completion_census_ptwrite_forms[ptwrite_index].form_id;
        BUSTER_TEST(arguments, selected);
    }
    for (u32 ptwrite_index = 0; ptwrite_index < BUSTER_ARRAY_LENGTH(x86_completion_census_ptwrite_forms); ptwrite_index += 1)
    {
        u32 ptwrite_form_id = x86_completion_census_ptwrite_forms[ptwrite_index].form_id;
        BusterX86CompletionCensusRecord const* before = &ptwrite_baseline_records[ptwrite_form_id];
        BusterX86CompletionCensusRecord const* after = &records[ptwrite_form_id];
        BUSTER_TEST(arguments, before->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }
    BUSTER_TEST(arguments, ptwrite_changed_count == BUSTER_ARRAY_LENGTH(x86_completion_census_ptwrite_forms));
    {
        static u8 const expected_ptwrite_register_bytes[] = {0xf3, 0x0f, 0xae, 0xe0};
        static u8 const expected_ptwrite_memory_bytes[] = {0xf3, 0x41, 0x0f, 0xae, 0x67, 0x40};
        AssemblyEncodeResult ptwrite_intel_register = assembly_encode(
            arguments->arena, S8("PTWRITE EAX\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult ptwrite_att_register = assembly_encode(
            arguments->arena, S8("ptwrite %eax\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult ptwrite_intel_memory = assembly_encode(
            arguments->arena, S8("PTWRITE DWORD PTR [R15+0x40]\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult ptwrite_att_memory = assembly_encode(
            arguments->arena, S8("ptwrite 0x40(%r15)\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, ptwrite_intel_register.diagnostic_count == 0 &&
                                 ptwrite_intel_register.bytes.length == sizeof(expected_ptwrite_register_bytes) &&
                                 memcmp(ptwrite_intel_register.bytes.pointer, expected_ptwrite_register_bytes,
                                        sizeof(expected_ptwrite_register_bytes)) == 0);
        BUSTER_TEST(arguments, ptwrite_att_register.diagnostic_count == 0 &&
                                 ptwrite_att_register.bytes.length == sizeof(expected_ptwrite_register_bytes) &&
                                 memcmp(ptwrite_att_register.bytes.pointer, expected_ptwrite_register_bytes,
                                        sizeof(expected_ptwrite_register_bytes)) == 0);
        BUSTER_TEST(arguments, ptwrite_intel_memory.diagnostic_count == 0 &&
                                 ptwrite_intel_memory.bytes.length == sizeof(expected_ptwrite_memory_bytes) &&
                                 memcmp(ptwrite_intel_memory.bytes.pointer, expected_ptwrite_memory_bytes,
                                        sizeof(expected_ptwrite_memory_bytes)) == 0);
        BUSTER_TEST(arguments, ptwrite_att_memory.diagnostic_count == 0 &&
                                 ptwrite_att_memory.bytes.length == sizeof(expected_ptwrite_memory_bytes) &&
                                 memcmp(ptwrite_att_memory.bytes.pointer, expected_ptwrite_memory_bytes,
                                        sizeof(expected_ptwrite_memory_bytes)) == 0);
        Target ptwrite_disabled_target = census_target;
        ptwrite_disabled_target.cpu_features = target_cpu_features_remove(ptwrite_disabled_target.cpu_features,
                                                                            TARGET_CPU_FEATURE_X86_PTWRITE);
        AssemblyEncodeResult ptwrite_disabled = assembly_encode(
            arguments->arena, S8("PTWRITE EAX\n"),
            (AssemblyEncodeOptions){.target = ptwrite_disabled_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        BUSTER_TEST(arguments, ptwrite_disabled.diagnostic_count != 0);
    }
    // ENQCMD has two normalized CPL3 rows (legacy and APX) plus their CPL0
    // ENQCMDS controls.  The feature-disabled comparison pins the generic
    // source result: Intel selects APX exactly and the legacy row as an
    // accepted alias; canonical AT&T source cannot force the metadata row's
    // explicit zero displacement, so both AT&T rows retain byte-mismatch.
    {
        u8 enqcmd_inventory_text[4 * 32] = {0};
        u32 enqcmd_inventory_length = 0;
        BusterX86MetadataPhysicalOperand enqcmd_topology_operands[2] = {
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
             .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
            {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 0,
             .memory = {.base = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
                        .address_size = 64, .scale = 1, .has_base = true}},
        };
        BusterX86MetadataPhysicalQuery enqcmd_topology_query = {
            .operands = enqcmd_topology_operands, .operand_count = 2, .address_size = 64,
            .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true,
            .source_semantics = true,
        };
        for (u32 enqcmd_index = 0; enqcmd_index < BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms); enqcmd_index += 1)
        {
            X86CompletionCensusFormKey const* expected = &x86_completion_census_enqcmd_forms[enqcmd_index];
            BusterX86MetadataForm enqcmd_form = {0};
            BusterX86MetadataFormKey enqcmd_key = {0};
            bool enqcmd_form_ok = buster_x86_metadata_form(expected->form_id, &enqcmd_form) &&
                                  buster_x86_metadata_form_key(expected->form_id, &enqcmd_key);
            bool enqcmd_apx = expected->form_id == 1749 || expected->form_id == 1750;
            bool enqcmd_privileged = expected->form_id == 1750 || expected->form_id == 8014;
            BUSTER_TEST(arguments, enqcmd_form_ok && enqcmd_key.stable_hash == expected->stable_hash &&
                                     string_equal(buster_x86_metadata_string_span(enqcmd_form.isa_set),
                                                  enqcmd_apx ? S8("APX_F_ENQCMD") : S8("ENQCMD")) &&
                                     buster_x86_metadata_string_span(enqcmd_form.cpl).length == 1 &&
                                     buster_x86_metadata_string_byte(enqcmd_form.cpl, 0) == (enqcmd_privileged ? '0' : '3') &&
                                     enqcmd_form.coverage_class == (enqcmd_privileged ? BUSTER_X86_METADATA_COVERAGE_PRIVILEGED :
                                                                     BUSTER_X86_METADATA_COVERAGE_NORMALIZED));
            String8 enqcmd_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                expected->form_id, expected->stable_hash);
            bool enqcmd_fits = enqcmd_inventory_length + enqcmd_line.length <= sizeof(enqcmd_inventory_text);
            BUSTER_TEST(arguments, enqcmd_fits);
            if (enqcmd_fits)
            {
                memcpy(enqcmd_inventory_text + enqcmd_inventory_length, enqcmd_line.pointer, enqcmd_line.length);
                enqcmd_inventory_length += (u32)enqcmd_line.length;
            }
        }
        u8 enqcmd_inventory_digest[32] = {0};
        static u8 const expected_enqcmd_inventory_digest[32] = {
            0x2b, 0x1f, 0xd3, 0xb0, 0x91, 0x21, 0xe1, 0x47,
            0x8d, 0x16, 0xb4, 0x3d, 0x73, 0x0c, 0x8b, 0x1b,
            0x98, 0x4d, 0xc3, 0x20, 0xec, 0xd8, 0x64, 0x86,
            0x79, 0x2b, 0x26, 0x78, 0x4e, 0x12, 0x64, 0xe8,
        };
        link_sha256(arguments->arena, enqcmd_inventory_text, enqcmd_inventory_length, enqcmd_inventory_digest);
        BUSTER_TEST(arguments, enqcmd_inventory_length == 88 &&
                                 memcmp(enqcmd_inventory_digest, expected_enqcmd_inventory_digest,
                                        sizeof(expected_enqcmd_inventory_digest)) == 0);
        bool enqcmd_seen[BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms)] = {0};
        u32 enqcmd_discovered_count = 0;
        for (u32 enqcmd_scan_form_id = 0; enqcmd_scan_form_id < form_count; enqcmd_scan_form_id += 1)
        {
            BusterX86MetadataForm enqcmd_scan_form = {0};
            if (!buster_x86_metadata_form(enqcmd_scan_form_id, &enqcmd_scan_form)) continue;
            String8 enqcmd_scan_isa = buster_x86_metadata_string_span(enqcmd_scan_form.isa_set);
            bool enqcmd_scan_match = string_equal(enqcmd_scan_isa, S8("ENQCMD")) ||
                                     string_equal(enqcmd_scan_isa, S8("APX_F_ENQCMD"));
            if (!enqcmd_scan_match) continue;
            enqcmd_discovered_count += 1;
            bool enqcmd_scan_expected = false;
            for (u32 enqcmd_scan_index = 0;
                 enqcmd_scan_index < BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms); enqcmd_scan_index += 1)
            {
                if (enqcmd_scan_form_id == x86_completion_census_enqcmd_forms[enqcmd_scan_index].form_id)
                {
                    enqcmd_seen[enqcmd_scan_index] = true;
                    enqcmd_scan_expected = true;
                }
            }
            BUSTER_TEST(arguments, enqcmd_scan_expected);
        }
        BUSTER_TEST(arguments, enqcmd_discovered_count == BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms));
        for (u32 enqcmd_seen_index = 0; enqcmd_seen_index < BUSTER_ARRAY_LENGTH(enqcmd_seen); enqcmd_seen_index += 1)
            BUSTER_TEST(arguments, enqcmd_seen[enqcmd_seen_index]);

        // The production source projection is deliberately topology-owned.
        // Scan every generated form with the canonical GPR64+ordinary-memory
        // source query and prove that the predicate has exactly the four
        // ENQCMD/ENQCMDS rows above—no census class or mnemonic filter can
        // hide a collateral match.
        u8 enqcmd_topology_inventory_text[4 * 32] = {0};
        u32 enqcmd_topology_inventory_length = 0;
        u32 enqcmd_topology_count = 0;
        bool enqcmd_topology_seen[BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms)] = {0};
        for (u32 enqcmd_topology_form_id = 0; enqcmd_topology_form_id < form_count; enqcmd_topology_form_id += 1)
        {
            BusterX86MetadataForm enqcmd_topology_form = {0};
            if (!buster_x86_metadata_form(enqcmd_topology_form_id, &enqcmd_topology_form)) continue;
            if (!buster_x86_metadata_aggregate_memory_source_topology(enqcmd_topology_form, enqcmd_topology_query)) continue;
            enqcmd_topology_count += 1;
            bool enqcmd_topology_expected = false;
            for (u32 enqcmd_topology_index = 0;
                 enqcmd_topology_index < BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms);
                 enqcmd_topology_index += 1)
            {
                X86CompletionCensusFormKey const* expected = &x86_completion_census_enqcmd_forms[enqcmd_topology_index];
                if (enqcmd_topology_form_id != expected->form_id) continue;
                enqcmd_topology_expected = true;
                enqcmd_topology_seen[enqcmd_topology_index] = true;
                BUSTER_TEST(arguments, enqcmd_topology_form.stable_hash == expected->stable_hash);
            }
            BUSTER_TEST(arguments, enqcmd_topology_expected);
            String8 enqcmd_topology_line = string_format(arguments->arena,
                                                          S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                          enqcmd_topology_form_id, enqcmd_topology_form.stable_hash);
            bool enqcmd_topology_fits = enqcmd_topology_inventory_length + enqcmd_topology_line.length <=
                                        sizeof(enqcmd_topology_inventory_text);
            BUSTER_TEST(arguments, enqcmd_topology_fits);
            if (enqcmd_topology_fits)
            {
                memcpy(enqcmd_topology_inventory_text + enqcmd_topology_inventory_length,
                       enqcmd_topology_line.pointer, enqcmd_topology_line.length);
                enqcmd_topology_inventory_length += (u32)enqcmd_topology_line.length;
            }
        }
        u8 enqcmd_topology_inventory_digest[32] = {0};
        link_sha256(arguments->arena, enqcmd_topology_inventory_text, enqcmd_topology_inventory_length,
                    enqcmd_topology_inventory_digest);
        BUSTER_TEST(arguments, enqcmd_topology_count == BUSTER_ARRAY_LENGTH(x86_completion_census_enqcmd_forms) &&
                                 enqcmd_topology_inventory_length == 88 &&
                                 memcmp(enqcmd_topology_inventory_digest, expected_enqcmd_inventory_digest,
                                        sizeof(expected_enqcmd_inventory_digest)) == 0);
        for (u32 enqcmd_topology_seen_index = 0;
             enqcmd_topology_seen_index < BUSTER_ARRAY_LENGTH(enqcmd_topology_seen); enqcmd_topology_seen_index += 1)
            BUSTER_TEST(arguments, enqcmd_topology_seen[enqcmd_topology_seen_index]);

        BusterX86MetadataPhysicalOperand enqcmd_bad_topology_operands[2] = {0};
        memcpy(enqcmd_bad_topology_operands, enqcmd_topology_operands, sizeof(enqcmd_bad_topology_operands));
        BusterX86MetadataPhysicalQuery enqcmd_bad_topology_query = enqcmd_topology_query;
        enqcmd_bad_topology_query.operands = enqcmd_bad_topology_operands;
        enqcmd_bad_topology_operands[0].reg.physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM;
        BusterX86MetadataForm enqcmd_topology_form1749 = {0};
        BusterX86MetadataForm enqcmd_topology_form8013 = {0};
        buster_x86_metadata_form(1749, &enqcmd_topology_form1749);
        buster_x86_metadata_form(8013, &enqcmd_topology_form8013);
        BUSTER_TEST(arguments, !buster_x86_metadata_aggregate_memory_source_topology(enqcmd_topology_form1749,
                                                                                       enqcmd_bad_topology_query) &&
                                 !buster_x86_metadata_aggregate_memory_source_topology(enqcmd_topology_form8013,
                                                                                       enqcmd_bad_topology_query));

        TemporalArena enqcmd_baseline_temporary = scratch_begin(&arguments->arena, 1);
        BusterX86CompletionCensusRecord* enqcmd_baseline_records =
            arena_allocate(enqcmd_baseline_temporary.arena, BusterX86CompletionCensusRecord, form_count);
        Target enqcmd_baseline_target = census_target;
        enqcmd_baseline_target.cpu_features = target_cpu_features_remove(
            enqcmd_baseline_target.cpu_features, TARGET_CPU_FEATURE_X86_ENQCMD);
        buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
            .arena = enqcmd_baseline_temporary.arena, .target = enqcmd_baseline_target,
            .records = enqcmd_baseline_records, .record_capacity = form_count,
            .run_intel = true, .run_att = true,
        });
        u32 enqcmd_changed_count = 0;
        for (u32 enqcmd_compare_form_id = 0; enqcmd_compare_form_id < form_count; enqcmd_compare_form_id += 1)
        {
            BusterX86CompletionCensusRecord const* before = &enqcmd_baseline_records[enqcmd_compare_form_id];
            BusterX86CompletionCensusRecord const* after = &records[enqcmd_compare_form_id];
            bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                           before->intel_source_reason != after->intel_source_reason ||
                           before->att_source_reason != after->att_source_reason ||
                           before->intel_byte_count != after->intel_byte_count ||
                           before->att_byte_count != after->att_byte_count ||
                           before->metadata_byte_count != after->metadata_byte_count;
            if (!changed) continue;
            enqcmd_changed_count += 1;
            BUSTER_TEST(arguments, enqcmd_compare_form_id == 1749 || enqcmd_compare_form_id == 8013);
        }
        BUSTER_TEST(arguments, enqcmd_changed_count == 2);
        BusterX86CompletionCensusRecord const* before_apx_enqcmd = &enqcmd_baseline_records[1749];
        BusterX86CompletionCensusRecord const* after_apx_enqcmd = &records[1749];
        BusterX86CompletionCensusRecord const* before_legacy_enqcmd = &enqcmd_baseline_records[8013];
        BusterX86CompletionCensusRecord const* after_legacy_enqcmd = &records[8013];
        BUSTER_TEST(arguments, before_apx_enqcmd->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_apx_enqcmd->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_apx_enqcmd->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before_apx_enqcmd->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after_apx_enqcmd->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after_apx_enqcmd->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                 after_apx_enqcmd->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_apx_enqcmd->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_apx_enqcmd->intel_byte_count == after_apx_enqcmd->metadata_byte_count &&
                                 after_apx_enqcmd->intel_byte_count == 7 && after_apx_enqcmd->att_byte_count == 6 &&
                                 after_apx_enqcmd->metadata_byte_count == 7);
        BUSTER_TEST(arguments, before_legacy_enqcmd->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_legacy_enqcmd->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_legacy_enqcmd->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before_legacy_enqcmd->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after_legacy_enqcmd->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT &&
                                 after_legacy_enqcmd->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                 after_legacy_enqcmd->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_legacy_enqcmd->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_legacy_enqcmd->intel_byte_count == after_legacy_enqcmd->metadata_byte_count &&
                                 after_legacy_enqcmd->intel_byte_count == 6 && after_legacy_enqcmd->att_byte_count == 5 &&
                                 after_legacy_enqcmd->metadata_byte_count == 6);
        for (u32 enqcmd_control_index = 0; enqcmd_control_index < 2; enqcmd_control_index += 1)
        {
            u32 enqcmd_control_form_id = enqcmd_control_index == 0 ? 1750 : 8014;
            BusterX86CompletionCensusRecord const* before = &enqcmd_baseline_records[enqcmd_control_form_id];
            BusterX86CompletionCensusRecord const* after = &records[enqcmd_control_form_id];
            BUSTER_TEST(arguments, before->intel_class == after->intel_class && before->att_class == after->att_class &&
                                     before->intel_source_reason == after->intel_source_reason &&
                                     before->att_source_reason == after->att_source_reason &&
                                     before->intel_byte_count == after->intel_byte_count &&
                                     before->att_byte_count == after->att_byte_count &&
                                     before->metadata_byte_count == after->metadata_byte_count);
        }
        scratch_end(enqcmd_baseline_temporary);
        {
            static u8 const expected_enqcmd_bytes[] = {0xf2, 0x0f, 0x38, 0xf8, 0x01};
            static u8 const expected_enqcmd_boundary_bytes[] = {0xf2, 0x45, 0x0f, 0x38, 0xf8, 0x7f, 0x40};
            static u8 const expected_apx_enqcmd_bytes[] = {0x62, 0xe4, 0x7f, 0x08, 0xf8, 0x00};
            static u8 const expected_apx_enqcmd_boundary_bytes[] = {0x62, 0x44, 0x7f, 0x08, 0xf8, 0x7f, 0x40};
            Target enqcmd_legacy_target = {
                .cpu_arch = CPU_ARCH_X86_64,
                .cpu_model = CPU_MODEL_INTEL_SAPPHIRE_RAPIDS,
                .os = OPERATING_SYSTEM_LINUX,
            };
            Target enqcmd_disabled_target = {
                .cpu_arch = CPU_ARCH_X86_64,
                .cpu_model = CPU_MODEL_BASELINE,
                .os = OPERATING_SYSTEM_LINUX,
                .cpu_features_explicit = true,
                .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
                    TARGET_CPU_FEATURE_X86_SSE2}, 1),
            };
            AssemblyEncodeResult enqcmd_intel = assembly_encode(
                arguments->arena, S8("enqcmd rax, zmmword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = enqcmd_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_intel_unsized = assembly_encode(
                arguments->arena, S8("enqcmd rax, [rcx]\n"),
                (AssemblyEncodeOptions){.target = enqcmd_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_att = assembly_encode(
                arguments->arena, S8("enqcmd (%rcx), %rax\n"),
                (AssemblyEncodeOptions){.target = enqcmd_legacy_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult enqcmd_intel_boundary = assembly_encode(
                arguments->arena, S8("enqcmd r15, zmmword ptr [r15+0x40]\n"),
                (AssemblyEncodeOptions){.target = enqcmd_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_att_boundary = assembly_encode(
                arguments->arena, S8("enqcmd 0x40(%r15), %r15\n"),
                (AssemblyEncodeOptions){.target = enqcmd_legacy_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult enqcmd_invalid_dword = assembly_encode(
                arguments->arena, S8("enqcmd rax, dword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_invalid_qword = assembly_encode(
                arguments->arena, S8("enqcmd rax, qword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_invalid_xmmword = assembly_encode(
                arguments->arena, S8("enqcmd rax, xmmword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_invalid_ymmword = assembly_encode(
                arguments->arena, S8("enqcmd rax, ymmword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult apx_enqcmd_intel = assembly_encode(
                arguments->arena, S8("enqcmd r16, zmmword ptr [rax]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult apx_enqcmd_att = assembly_encode(
                arguments->arena, S8("enqcmd (%rax), %r16\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            AssemblyEncodeResult apx_enqcmd_intel_boundary = assembly_encode(
                arguments->arena, S8("enqcmd r31, zmmword ptr [r15+0x40]\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult apx_enqcmd_att_boundary = assembly_encode(
                arguments->arena, S8("enqcmd 0x40(%r15), %r31\n"),
                (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, enqcmd_intel.diagnostic_count == 0 && enqcmd_intel_unsized.diagnostic_count == 0 &&
                                     enqcmd_att.diagnostic_count == 0 &&
                                     enqcmd_intel_boundary.diagnostic_count == 0 && enqcmd_att_boundary.diagnostic_count == 0 &&
                                     enqcmd_intel.bytes.length == sizeof(expected_enqcmd_bytes) &&
                                     enqcmd_intel_unsized.bytes.length == sizeof(expected_enqcmd_bytes) &&
                                     enqcmd_att.bytes.length == sizeof(expected_enqcmd_bytes) &&
                                     enqcmd_intel_boundary.bytes.length == sizeof(expected_enqcmd_boundary_bytes) &&
                                     enqcmd_att_boundary.bytes.length == sizeof(expected_enqcmd_boundary_bytes) &&
                                     memcmp(enqcmd_intel.bytes.pointer, expected_enqcmd_bytes, sizeof(expected_enqcmd_bytes)) == 0 &&
                                     memcmp(enqcmd_intel_unsized.bytes.pointer, expected_enqcmd_bytes, sizeof(expected_enqcmd_bytes)) == 0 &&
                                     memcmp(enqcmd_att.bytes.pointer, expected_enqcmd_bytes, sizeof(expected_enqcmd_bytes)) == 0 &&
                                     memcmp(enqcmd_intel_boundary.bytes.pointer, expected_enqcmd_boundary_bytes,
                                            sizeof(expected_enqcmd_boundary_bytes)) == 0 &&
                                     memcmp(enqcmd_att_boundary.bytes.pointer, expected_enqcmd_boundary_bytes,
                                            sizeof(expected_enqcmd_boundary_bytes)) == 0);
            BUSTER_TEST(arguments, enqcmd_invalid_dword.diagnostic_count != 0 && enqcmd_invalid_dword.bytes.length == 0 &&
                                     enqcmd_invalid_qword.diagnostic_count != 0 && enqcmd_invalid_qword.bytes.length == 0 &&
                                     enqcmd_invalid_xmmword.diagnostic_count != 0 && enqcmd_invalid_xmmword.bytes.length == 0 &&
                                     enqcmd_invalid_ymmword.diagnostic_count != 0 && enqcmd_invalid_ymmword.bytes.length == 0);
            BUSTER_TEST(arguments, apx_enqcmd_intel.diagnostic_count == 0 && apx_enqcmd_att.diagnostic_count == 0 &&
                                     apx_enqcmd_intel_boundary.diagnostic_count == 0 && apx_enqcmd_att_boundary.diagnostic_count == 0 &&
                                     apx_enqcmd_intel.bytes.length == sizeof(expected_apx_enqcmd_bytes) &&
                                     apx_enqcmd_att.bytes.length == sizeof(expected_apx_enqcmd_bytes) &&
                                     apx_enqcmd_intel_boundary.bytes.length == sizeof(expected_apx_enqcmd_boundary_bytes) &&
                                     apx_enqcmd_att_boundary.bytes.length == sizeof(expected_apx_enqcmd_boundary_bytes) &&
                                     memcmp(apx_enqcmd_intel.bytes.pointer, expected_apx_enqcmd_bytes,
                                            sizeof(expected_apx_enqcmd_bytes)) == 0 &&
                                     memcmp(apx_enqcmd_att.bytes.pointer, expected_apx_enqcmd_bytes,
                                            sizeof(expected_apx_enqcmd_bytes)) == 0 &&
                                     memcmp(apx_enqcmd_intel_boundary.bytes.pointer, expected_apx_enqcmd_boundary_bytes,
                                            sizeof(expected_apx_enqcmd_boundary_bytes)) == 0 &&
                                     memcmp(apx_enqcmd_att_boundary.bytes.pointer, expected_apx_enqcmd_boundary_bytes,
                                            sizeof(expected_apx_enqcmd_boundary_bytes)) == 0);
            AssemblyEncodeResult enqcmd_disabled_intel = assembly_encode(
                arguments->arena, S8("enqcmd rax, zmmword ptr [rcx]\n"),
                (AssemblyEncodeOptions){.target = enqcmd_disabled_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
            AssemblyEncodeResult enqcmd_disabled_att = assembly_encode(
                arguments->arena, S8("enqcmd (%rcx), %rax\n"),
                (AssemblyEncodeOptions){.target = enqcmd_disabled_target, .syntax = ASSEMBLY_SYNTAX_ATT});
            BUSTER_TEST(arguments, enqcmd_disabled_intel.diagnostic_count != 0 && enqcmd_disabled_att.diagnostic_count != 0);
        }
    }
    // The block-transfer inventory has one normalized APX form, one
    // normalized legacy64 form, and one non-64 control.  With the full
    // Diamond Rapids target, Intel source selects the APX encoding while the
    // AT&T projection and the legacy row remain byte-mismatch controls.  The
    // non-64 form remains outside the x86-64 source partition.
    u8 movdir64b_inventory_text[3 * 64] = {0};
    u32 movdir64b_inventory_length = 0;
    for (u32 movdir64b_index = 0;
         movdir64b_index < BUSTER_ARRAY_LENGTH(x86_completion_census_movdir64b_forms);
         movdir64b_index += 1)
    {
        X86CompletionCensusFormKey const* expected = &x86_completion_census_movdir64b_forms[movdir64b_index];
        BusterX86MetadataForm form = {0};
        BusterX86MetadataFormKey key = {0};
        bool form_ok = buster_x86_metadata_form(expected->form_id, &form) &&
                       buster_x86_metadata_form_key(expected->form_id, &key);
        BUSTER_TEST(arguments, form_ok && key.stable_hash == expected->stable_hash &&
                                 ((expected->form_id == 1886 &&
                                   string_equal(buster_x86_metadata_string_span(form.isa_set), S8("APX_F_MOVDIR64B"))) ||
                                  (expected->form_id != 1886 &&
                                   string_equal(buster_x86_metadata_string_span(form.isa_set), S8("MOVDIR64B")))) &&
                                 buster_x86_metadata_string_span(form.cpl).length == 1 &&
                                 buster_x86_metadata_string_byte(form.cpl, 0) == '3');
        if (expected->form_id == 8762)
            BUSTER_TEST(arguments, form_ok && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NOT64);
        else
            BUSTER_TEST(arguments, form_ok && form.coverage_class == BUSTER_X86_METADATA_COVERAGE_NORMALIZED);
        String8 line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                      expected->form_id, expected->stable_hash);
        bool fits = movdir64b_inventory_length + line.length <= sizeof(movdir64b_inventory_text);
        BUSTER_TEST(arguments, fits);
        if (fits)
        {
            memcpy(movdir64b_inventory_text + movdir64b_inventory_length, line.pointer, line.length);
            movdir64b_inventory_length += (u32)line.length;
        }
    }
    BUSTER_TEST(arguments, movdir64b_inventory_length == 66);
    // Exhaustively apply the production topology predicate to every
    // generated form with a matching unsized GPR64 + ordinary-memory query.
    // Exactly two normalized block-transfer forms satisfy the semantic shape:
    // APX1886 and legacy8763.  Both accept the architectural zmmword
    // aggregate qualifier; every other explicit width is rejected before a
    // shadow candidate can be selected.
    BusterX86MetadataPhysicalOperand block_memory_topology_operands[2] = {
        {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
         .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR}},
        {.kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY, .width = 0,
         .memory = {.base = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
                    .address_size = 64, .has_base = true}},
    };
    BusterX86MetadataPhysicalQuery block_memory_topology_query = {
        .operands = block_memory_topology_operands, .operand_count = 2, .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64, .include_privileged = true,
        .source_semantics = true,
    };
    u8 block_memory_topology_text[64] = {0};
    u32 block_memory_topology_length = 0;
    u32 block_memory_topology_count = 0;
    u32 block_memory_topology_form_ids[2] = {0};
    u64 block_memory_topology_hashes[2] = {0};
    for (u32 block_memory_form_id = 0; block_memory_form_id < form_count; block_memory_form_id += 1)
    {
        BusterX86MetadataForm block_memory_form = {0};
        if (!buster_x86_metadata_form(block_memory_form_id, &block_memory_form)) continue;
        if (!buster_x86_metadata_block_memory_source_topology(block_memory_form, block_memory_topology_query)) continue;
        block_memory_topology_count += 1;
        if (block_memory_topology_count <= BUSTER_ARRAY_LENGTH(block_memory_topology_form_ids))
        {
            block_memory_topology_form_ids[block_memory_topology_count - 1] = block_memory_form_id;
            block_memory_topology_hashes[block_memory_topology_count - 1] = block_memory_form.stable_hash;
        }
        String8 block_memory_line = string_format(arguments->arena, S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                   block_memory_form_id, block_memory_form.stable_hash);
        bool block_memory_fits = block_memory_topology_length + block_memory_line.length <= sizeof(block_memory_topology_text);
        BUSTER_TEST(arguments, block_memory_fits);
        if (block_memory_fits)
        {
            memcpy(block_memory_topology_text + block_memory_topology_length,
                   block_memory_line.pointer, block_memory_line.length);
            block_memory_topology_length += (u32)block_memory_line.length;
        }
    }
    u8 block_memory_topology_digest[32] = {0};
    static u8 const expected_block_memory_topology_digest[32] = {
        0xd0, 0x43, 0x4e, 0x20, 0xd0, 0x5e, 0x17, 0x30,
        0x70, 0x8d, 0x02, 0x49, 0xb6, 0x18, 0x4a, 0x87,
        0x5a, 0x96, 0xad, 0x87, 0xab, 0x2a, 0x94, 0xb6,
        0xc9, 0x3c, 0x33, 0x3b, 0x0d, 0x39, 0x4c, 0x4b,
    };
    link_sha256(arguments->arena, block_memory_topology_text, block_memory_topology_length,
                block_memory_topology_digest);
    BUSTER_TEST(arguments, block_memory_topology_count == 2 && block_memory_topology_form_ids[0] == 1886 &&
                             block_memory_topology_hashes[0] == UINT64_C(0xcfdf1526a8946534) &&
                             block_memory_topology_form_ids[1] == 8763 &&
                             block_memory_topology_hashes[1] == UINT64_C(0xd4b2dade382c430e) &&
                             block_memory_topology_length == 44 &&
                             memcmp(block_memory_topology_digest, expected_block_memory_topology_digest,
                                    sizeof(expected_block_memory_topology_digest)) == 0);
    block_memory_topology_operands[1].memory.source_width = 512;
    BusterX86MetadataForm legacy_memory_form = {0};
    BusterX86MetadataForm apx_memory_form = {0};
    BUSTER_TEST(arguments, buster_x86_metadata_form(1886, &apx_memory_form));
    BUSTER_TEST(arguments, buster_x86_metadata_form(8763, &legacy_memory_form));
    BUSTER_TEST(arguments, buster_x86_metadata_block_memory_source_authoritative(apx_memory_form,
                                                                                  block_memory_topology_query));
    BUSTER_TEST(arguments, buster_x86_metadata_block_memory_source_authoritative(legacy_memory_form,
                                                                                  block_memory_topology_query));
    block_memory_topology_operands[1].memory.source_width = 32;
    BUSTER_TEST(arguments, !buster_x86_metadata_block_memory_source_authoritative(apx_memory_form,
                                                                                   block_memory_topology_query));
    BUSTER_TEST(arguments, !buster_x86_metadata_block_memory_source_authoritative(legacy_memory_form,
                                                                                   block_memory_topology_query));
    block_memory_topology_operands[1].memory.source_width = 512;
    BusterX86CompletionCensusRecord* movdir64b_baseline_records =
        arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target movdir64b_baseline_target = census_target;
    movdir64b_baseline_target.cpu_features = target_cpu_features_remove(
        movdir64b_baseline_target.cpu_features, TARGET_CPU_FEATURE_X86_MOVDIR64B);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = movdir64b_baseline_target,
        .records = movdir64b_baseline_records, .record_capacity = form_count,
        .run_intel = true, .run_att = true,
    });
    u32 movdir64b_changed_count = 0;
    for (u32 movdir64b_compare_form_id = 0; movdir64b_compare_form_id < form_count;
         movdir64b_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &movdir64b_baseline_records[movdir64b_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[movdir64b_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason ||
                       before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count ||
                       before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (!changed) continue;
        movdir64b_changed_count += 1;
        bool selected = false;
        for (u32 movdir64b_index = 0;
             movdir64b_index < BUSTER_ARRAY_LENGTH(x86_completion_census_movdir64b_forms);
             movdir64b_index += 1)
            selected |= movdir64b_compare_form_id == x86_completion_census_movdir64b_forms[movdir64b_index].form_id;
        BUSTER_TEST(arguments, selected);
    }
    BUSTER_TEST(arguments, movdir64b_changed_count == 2);
    {
        BusterX86CompletionCensusRecord const* before_apx = &movdir64b_baseline_records[1886];
        BusterX86CompletionCensusRecord const* after_apx = &records[1886];
        BusterX86CompletionCensusRecord const* before_non64 = &movdir64b_baseline_records[8762];
        BusterX86CompletionCensusRecord const* after_non64 = &records[8762];
        BusterX86CompletionCensusRecord const* before_legacy = &movdir64b_baseline_records[8763];
        BusterX86CompletionCensusRecord const* after_legacy = &records[8763];
        BUSTER_TEST(arguments, before_apx->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_apx->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_apx->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before_apx->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after_apx->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 after_apx->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_apx->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                 after_apx->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        BUSTER_TEST(arguments, before_legacy->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_legacy->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 before_legacy->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 before_legacy->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                 after_legacy->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                 after_legacy->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                 after_legacy->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 after_legacy->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        BUSTER_TEST(arguments, before_non64->intel_class == after_non64->intel_class &&
                                 before_non64->att_class == after_non64->att_class &&
                                 before_non64->intel_source_reason == after_non64->intel_source_reason &&
                                 before_non64->att_source_reason == after_non64->att_source_reason &&
                                 before_non64->intel_byte_count == after_non64->intel_byte_count &&
                                 before_non64->att_byte_count == after_non64->att_byte_count &&
                                 before_non64->metadata_byte_count == after_non64->metadata_byte_count);
    }
    {
        static u8 const expected_movdir64b_bytes[] = {0x66, 0x0f, 0x38, 0xf8, 0x01};
        static u8 const expected_movdir64b_boundary_bytes[] = {0x66, 0x45, 0x0f, 0x38, 0xf8, 0x7f, 0x40};
        Target movdir64b_legacy_target = {
            .cpu_arch = CPU_ARCH_X86_64,
            .cpu_model = CPU_MODEL_BASELINE,
            .os = OPERATING_SYSTEM_LINUX,
            .cpu_features_explicit = true,
            .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_MOVDIR64B}, 2),
        };
        AssemblyEncodeResult intel_rcx = assembly_encode(
            arguments->arena, S8("movdir64b rax, [rcx]\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult intel_zmm_rcx = assembly_encode(
            arguments->arena, S8("movdir64b rax, zmmword ptr [rcx]\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult intel_dword_rcx = assembly_encode(
            arguments->arena, S8("movdir64b rax, dword ptr [rcx]\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult intel_dword_full = assembly_encode(
            arguments->arena, S8("movdir64b rax, dword ptr [rcx]\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult att_rcx = assembly_encode(
            arguments->arena, S8("movdir64b (%rcx), %rax\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        AssemblyEncodeResult intel_boundary = assembly_encode(
            arguments->arena, S8("movdir64b r15, [r15+0x40]\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult att_boundary = assembly_encode(
            arguments->arena, S8("movdir64b 0x40(%r15), %r15\n"),
            (AssemblyEncodeOptions){.target = movdir64b_legacy_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, intel_rcx.diagnostic_count == 0 && intel_zmm_rcx.diagnostic_count == 0 &&
                                 intel_dword_rcx.diagnostic_count != 0 && intel_dword_full.diagnostic_count != 0 &&
                                 att_rcx.diagnostic_count == 0 &&
                                 intel_rcx.bytes.length == sizeof(expected_movdir64b_bytes) &&
                                 intel_zmm_rcx.bytes.length == sizeof(expected_movdir64b_bytes) &&
                                 att_rcx.bytes.length == sizeof(expected_movdir64b_bytes) &&
                                 memcmp(intel_rcx.bytes.pointer, expected_movdir64b_bytes, sizeof(expected_movdir64b_bytes)) == 0 &&
                                 memcmp(intel_zmm_rcx.bytes.pointer, expected_movdir64b_bytes, sizeof(expected_movdir64b_bytes)) == 0 &&
                                 memcmp(att_rcx.bytes.pointer, expected_movdir64b_bytes, sizeof(expected_movdir64b_bytes)) == 0);
        BUSTER_TEST(arguments, intel_boundary.diagnostic_count == 0 && att_boundary.diagnostic_count == 0 &&
                                 intel_boundary.bytes.length == sizeof(expected_movdir64b_boundary_bytes) &&
                                 att_boundary.bytes.length == sizeof(expected_movdir64b_boundary_bytes) &&
                                 memcmp(intel_boundary.bytes.pointer, expected_movdir64b_boundary_bytes,
                                        sizeof(expected_movdir64b_boundary_bytes)) == 0 &&
                                 memcmp(att_boundary.bytes.pointer, expected_movdir64b_boundary_bytes,
                                        sizeof(expected_movdir64b_boundary_bytes)) == 0);
        Target movdir64b_disabled_target = movdir64b_legacy_target;
        movdir64b_disabled_target.cpu_features = target_cpu_features_remove(
            movdir64b_disabled_target.cpu_features, TARGET_CPU_FEATURE_X86_MOVDIR64B);
        AssemblyEncodeResult disabled_intel = assembly_encode(
            arguments->arena, S8("movdir64b rax, [rcx]\n"),
            (AssemblyEncodeOptions){.target = movdir64b_disabled_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult disabled_att = assembly_encode(
            arguments->arena, S8("movdir64b (%rcx), %rax\n"),
            (AssemblyEncodeOptions){.target = movdir64b_disabled_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        BUSTER_TEST(arguments, disabled_intel.diagnostic_count != 0 && disabled_att.diagnostic_count != 0);
    }
    for (u32 crypto_shadow_index = 0;
         crypto_shadow_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sm4_evex_shadow_forms);
         crypto_shadow_index += 1)
    {
        X86CompletionCensusFormKey const* crypto_expected = &x86_completion_census_sm4_evex_shadow_forms[crypto_shadow_index];
        u32 crypto_form_id = crypto_expected->form_id;
        BusterX86MetadataFormKey crypto_key = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form_key(crypto_form_id, &crypto_key) && crypto_key.stable_hash == crypto_expected->stable_hash);
    }
    u32 intel_reason_total = 0;
    u32 att_reason_total = 0;
    u32 intel_reason_non_none = 0;
    u32 att_reason_non_none = 0;
    for (class_index = 0; class_index < BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT; class_index += 1)
    {
        intel_reason_total += source.intel_source_reason_counts[class_index];
        att_reason_total += source.att_source_reason_counts[class_index];
    }
    for (class_index = 1; class_index < BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT; class_index += 1)
    {
        intel_reason_non_none += source.intel_source_reason_counts[class_index];
        att_reason_non_none += source.att_source_reason_counts[class_index];
    }
    BusterX86CompletionCensusRecord* crypto_baseline_records = arena_allocate(arguments->arena, BusterX86CompletionCensusRecord, form_count);
    Target crypto_baseline_target = census_target;
    crypto_baseline_target.cpu_features = target_cpu_features_remove(crypto_baseline_target.cpu_features, TARGET_CPU_FEATURE_X86_SHA512);
    crypto_baseline_target.cpu_features = target_cpu_features_remove(crypto_baseline_target.cpu_features, TARGET_CPU_FEATURE_X86_SM3);
    crypto_baseline_target.cpu_features = target_cpu_features_remove(crypto_baseline_target.cpu_features, TARGET_CPU_FEATURE_X86_SM4);
    buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arguments->arena, .target = crypto_baseline_target, .records = crypto_baseline_records,
        .record_capacity = form_count, .run_intel = true, .run_att = true,
    });
    u32 crypto_changed_count = 0;
    u32 crypto_changed_exact_count = 0;
    u32 crypto_changed_shadow_count = 0;
    for (u32 crypto_compare_form_id = 0; crypto_compare_form_id < form_count; crypto_compare_form_id += 1)
    {
        BusterX86CompletionCensusRecord const* before = &crypto_baseline_records[crypto_compare_form_id];
        BusterX86CompletionCensusRecord const* after = &records[crypto_compare_form_id];
        bool changed = before->intel_class != after->intel_class || before->att_class != after->att_class ||
                       before->intel_source_reason != after->intel_source_reason || before->att_source_reason != after->att_source_reason ||
                       before->intel_byte_count != after->intel_byte_count || before->att_byte_count != after->att_byte_count ||
                       before->metadata_byte_count != after->metadata_byte_count;
        if (changed)
        {
            crypto_changed_count += 1;
            bool exact_control = false;
            for (u32 crypto_control_index = 0;
                 crypto_control_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sha512_sm3_sm4_forms);
                 crypto_control_index += 1)
            {
                exact_control |= crypto_compare_form_id == x86_completion_census_sha512_sm3_sm4_forms[crypto_control_index].form_id;
            }
            bool shadow_control = false;
            for (u32 crypto_shadow_index = 0;
                 crypto_shadow_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sm4_evex_shadow_forms);
                 crypto_shadow_index += 1)
            {
                shadow_control |= crypto_compare_form_id == x86_completion_census_sm4_evex_shadow_forms[crypto_shadow_index].form_id;
            }
            BUSTER_TEST(arguments, exact_control || shadow_control);
            if (exact_control)
            {
                crypto_changed_exact_count += 1;
                BUSTER_TEST(arguments, after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                         after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                         after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                         after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
            }
            if (shadow_control)
            {
                crypto_changed_shadow_count += 1;
                BUSTER_TEST(arguments, after->intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                         after->att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH &&
                                         after->intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                         after->att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                         after->intel_relocation_count == after->metadata_relocation_count &&
                                         after->att_relocation_count == after->metadata_relocation_count);
            }
        }
    }
    BUSTER_TEST(arguments, crypto_changed_count == 25 && crypto_changed_exact_count == 17 && crypto_changed_shadow_count == 8);
    for (u32 crypto_zmm_index = 0;
         crypto_zmm_index < BUSTER_ARRAY_LENGTH(x86_completion_census_sm4_zmm_control_forms);
         crypto_zmm_index += 1)
    {
        u32 crypto_form_id = x86_completion_census_sm4_zmm_control_forms[crypto_zmm_index].form_id;
        BusterX86MetadataFormKey crypto_zmm_key = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form_key(crypto_form_id, &crypto_zmm_key) &&
                                 crypto_zmm_key.stable_hash == x86_completion_census_sm4_zmm_control_forms[crypto_zmm_index].stable_hash);
        BusterX86CompletionCensusRecord const* before = &crypto_baseline_records[crypto_form_id];
        BusterX86CompletionCensusRecord const* after = &records[crypto_form_id];
        BUSTER_TEST(arguments, before->intel_class == after->intel_class && before->att_class == after->att_class &&
                                 before->intel_source_reason == after->intel_source_reason &&
                                 before->att_source_reason == after->att_source_reason &&
                                 before->intel_byte_count == after->intel_byte_count &&
                                 before->att_byte_count == after->att_byte_count &&
                                 before->metadata_byte_count == after->metadata_byte_count &&
                                 before->intel_relocation_count == after->intel_relocation_count &&
                                 before->att_relocation_count == after->att_relocation_count);
    }

    // The typed source-builder and standalone-SAE inventories are derived
    // from metadata topology, then hashed over their sorted dense IDs.  This
    // keeps the exhaustive cohort contract independent of mnemonic spelling
    // or a hand-maintained partial witness list.
    u8 typed_inventory_text[1410 * 12] = {0};
    u8 standalone_inventory_text[32 * 12] = {0};
    u8 typed_inventory_hash_text[1410 * 32] = {0};
    u8 standalone_inventory_hash_text[32 * 32] = {0};
    u8 avx10_family_hash_text[130 * 32] = {0};
    u8 control_inventory_hash_text[74 * 32] = {0};
    u32 typed_inventory_length = 0;
    u32 standalone_inventory_length = 0;
    u32 typed_inventory_hash_length = 0;
    u32 standalone_inventory_hash_length = 0;
    u32 avx10_family_hash_length = 0;
    u32 control_inventory_hash_length = 0;
    u32 typed_inventory_count = 0;
    u32 standalone_inventory_count = 0;
    u32 avx10_family_count = 0;
    u32 avx10_family_counts[4] = {0};
    u32 control_inventory_count = 0;
    u32 control_inventory_counts[4] = {0};
    u32 typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT][BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT] = {0};
    for (u32 inventory_form_id = 0; inventory_form_id < form_count; inventory_form_id += 1)
    {
        if (buster_x86_completion_census_test_decorator_shape(inventory_form_id, false))
        {
            BusterX86MetadataForm inventory_form = {0};
            BusterX86MetadataFormKey inventory_key = {0};
            String8 id_text = string_format(arguments->arena, S8("{u32}\n"), inventory_form_id);
            String8 hash_text = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_form(inventory_form_id, &inventory_form) &&
                                     buster_x86_metadata_form_key(inventory_form_id, &inventory_key));
            hash_text = string_format(arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"), inventory_form_id,
                                      inventory_key.stable_hash);
            BUSTER_TEST(arguments, typed_inventory_length + id_text.length <= sizeof(typed_inventory_text));
            BUSTER_TEST(arguments, typed_inventory_hash_length + hash_text.length <= sizeof(typed_inventory_hash_text));
            memcpy(typed_inventory_text + typed_inventory_length, id_text.pointer, id_text.length);
            typed_inventory_length += (u32)id_text.length;
            memcpy(typed_inventory_hash_text + typed_inventory_hash_length, hash_text.pointer, hash_text.length);
            typed_inventory_hash_length += (u32)hash_text.length;
            typed_inventory_count += 1;
            typed_pair_matrix[records[inventory_form_id].intel_class][records[inventory_form_id].att_class] += 1;
            // The AVX10.2 feature-map cohort is a semantic intersection of
            // the typed topology and the generated ISA family.  Classify
            // DS before SAT_CVT because the former has the latter as a text
            // prefix; no dense form identity participates in this rule.
            String8 isa_set = buster_x86_metadata_string_span(inventory_form.isa_set);
            u32 avx10_family = UINT32_MAX;
            if (string_starts_with_sequence(isa_set, S8("AVX512_FP8_CONVERT_"))) avx10_family = 0;
            else if (string_starts_with_sequence(isa_set, S8("AVX512_MINMAX_"))) avx10_family = 1;
            else if (string_starts_with_sequence(isa_set, S8("AVX512_SAT_CVT_DS_"))) avx10_family = 3;
            else if (string_starts_with_sequence(isa_set, S8("AVX512_SAT_CVT_"))) avx10_family = 2;
            if (avx10_family != UINT32_MAX)
            {
                String8 family_hash_text = string_format(
                    arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"), inventory_form_id,
                    inventory_key.stable_hash);
                BUSTER_TEST(arguments, avx10_family_hash_length + family_hash_text.length <= sizeof(avx10_family_hash_text));
                memcpy(avx10_family_hash_text + avx10_family_hash_length, family_hash_text.pointer, family_hash_text.length);
                avx10_family_hash_length += (u32)family_hash_text.length;
                avx10_family_count += 1;
                avx10_family_counts[avx10_family] += 1;
            }
        }
        if (buster_x86_completion_census_test_decorator_shape(inventory_form_id, true))
        {
            BusterX86MetadataForm inventory_form = {0};
            BusterX86MetadataFormKey inventory_key = {0};
            String8 id_text = string_format(arguments->arena, S8("{u32}\n"), inventory_form_id);
            String8 hash_text = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_form(inventory_form_id, &inventory_form) &&
                                     buster_x86_metadata_form_key(inventory_form_id, &inventory_key));
            hash_text = string_format(arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"), inventory_form_id,
                                      inventory_key.stable_hash);
            BUSTER_TEST(arguments, standalone_inventory_length + id_text.length <= sizeof(standalone_inventory_text));
            BUSTER_TEST(arguments, standalone_inventory_hash_length + hash_text.length <= sizeof(standalone_inventory_hash_text));
            memcpy(standalone_inventory_text + standalone_inventory_length, id_text.pointer, id_text.length);
            standalone_inventory_length += (u32)id_text.length;
            memcpy(standalone_inventory_hash_text + standalone_inventory_hash_length, hash_text.pointer, hash_text.length);
            standalone_inventory_hash_length += (u32)hash_text.length;
            standalone_inventory_count += 1;
        }
        if (buster_x86_completion_census_test_control_prefix_shape(inventory_form_id))
        {
            BusterX86MetadataForm inventory_form = {0};
            BusterX86MetadataFormKey inventory_key = {0};
            BusterX86MetadataPhysicalQuery inventory_query = {0};
            BusterX86MetadataPhysicalOperand inventory_operands[16] = {0};
            String8 inventory_features[1] = {0};
            char8 inventory_mnemonic_buffer[128] = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_form(inventory_form_id, &inventory_form) &&
                                     buster_x86_metadata_form_key(inventory_form_id, &inventory_key) &&
                                     buster_x86_completion_census_test_query(inventory_form_id, &inventory_query,
                                                                              inventory_operands, inventory_features,
                                                                              inventory_mnemonic_buffer));
            String8 hash_text = string_format(arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"),
                                              inventory_form_id, inventory_key.stable_hash);
            bool control_hash_fits = control_inventory_hash_length + hash_text.length <= sizeof(control_inventory_hash_text);
            BUSTER_TEST(arguments, control_hash_fits);
            if (!control_hash_fits) continue;
            memcpy(control_inventory_hash_text + control_inventory_hash_length, hash_text.pointer, hash_text.length);
            control_inventory_hash_length += (u32)hash_text.length;
            control_inventory_count += 1;
            control_inventory_counts[0] += inventory_query.attributes.lock;
            control_inventory_counts[1] += inventory_query.attributes.rep;
            control_inventory_counts[2] += inventory_query.attributes.repne;
            control_inventory_counts[3] += inventory_query.attributes.notrack;
            BUSTER_TEST(arguments,
                        records[inventory_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                            records[inventory_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                            records[inventory_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                            records[inventory_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        }
    }
    u8 typed_inventory_digest[32] = {0};
    u8 standalone_inventory_digest[32] = {0};
    u8 typed_inventory_hash_digest[32] = {0};
    u8 standalone_inventory_hash_digest[32] = {0};
    u8 avx10_family_digest[32] = {0};
    u8 control_inventory_digest[32] = {0};
    link_sha256(arguments->arena, typed_inventory_text, typed_inventory_length, typed_inventory_digest);
    link_sha256(arguments->arena, standalone_inventory_text, standalone_inventory_length, standalone_inventory_digest);
    link_sha256(arguments->arena, typed_inventory_hash_text, typed_inventory_hash_length, typed_inventory_hash_digest);
    link_sha256(arguments->arena, standalone_inventory_hash_text, standalone_inventory_hash_length, standalone_inventory_hash_digest);
    link_sha256(arguments->arena, avx10_family_hash_text, avx10_family_hash_length, avx10_family_digest);
    link_sha256(arguments->arena, control_inventory_hash_text, control_inventory_hash_length, control_inventory_digest);
    static u8 const expected_typed_inventory_digest[32] = {
        0x4a, 0x25, 0x3c, 0x4e, 0x2e, 0x9a, 0x06, 0xd7, 0x69, 0xa9, 0x4f, 0x36, 0xb2, 0x6f, 0xd3, 0x85,
        0x7e, 0x46, 0x86, 0x27, 0xae, 0x66, 0xce, 0x00, 0x19, 0x07, 0xc8, 0xe6, 0x95, 0xa0, 0xf8, 0xfa,
    };
    static u8 const expected_standalone_inventory_digest[32] = {
        0x15, 0x3b, 0xe7, 0x5e, 0x44, 0xdf, 0x30, 0x41, 0xe0, 0x4e, 0x2f, 0xe7, 0xa6, 0x86, 0xc0, 0xbf,
        0xfd, 0x53, 0x3c, 0x94, 0x0e, 0x63, 0xbe, 0x8c, 0xbb, 0x9e, 0x8a, 0x4b, 0x26, 0x8e, 0xf8, 0xbc,
    };
    static u8 const expected_typed_inventory_hash_digest[32] = {
        0x25, 0x77, 0x72, 0xc7, 0xd1, 0x80, 0xce, 0x72, 0x8d, 0xb4, 0xba, 0x54, 0x2a, 0x67, 0xce, 0x4f,
        0x7a, 0x93, 0xaa, 0xa5, 0xe1, 0x89, 0x65, 0xbd, 0x0c, 0xd2, 0x77, 0x2b, 0x91, 0x2b, 0x5b, 0x7d,
    };
    static u8 const expected_standalone_inventory_hash_digest[32] = {
        0x15, 0x1f, 0x9e, 0xda, 0x2f, 0xfa, 0xb2, 0x20, 0x0e, 0xf6, 0xd4, 0x87, 0x7d, 0xe1, 0x82, 0x9d,
        0x86, 0x27, 0x4e, 0x47, 0x5f, 0x34, 0x4d, 0xfe, 0x06, 0x7d, 0xc5, 0xd0, 0xca, 0x7d, 0x34, 0x1e,
    };
    static u8 const expected_avx10_family_digest[32] = {
        0xd3, 0x0e, 0x72, 0x2b, 0xa3, 0xfc, 0xec, 0x63, 0xa7, 0x49, 0xf9, 0x87, 0x36, 0x97, 0x9b, 0xb5,
        0x79, 0xfd, 0x4e, 0x8c, 0x83, 0x44, 0xa9, 0xfe, 0x98, 0x21, 0x19, 0x98, 0x2f, 0x3c, 0xca, 0xf1,
    };
    static u8 const expected_control_inventory_digest[32] = {
        0xfd, 0x7b, 0x2a, 0x6b, 0x29, 0xd7, 0x98, 0x59, 0x0e, 0x70, 0x92, 0xe1, 0x3f, 0xd4, 0x12, 0xfe,
        0xac, 0x68, 0x9f, 0x39, 0x01, 0x57, 0xbb, 0x28, 0xcf, 0x37, 0x4b, 0xd3, 0x16, 0x7b, 0x25, 0x85,
    };
    BUSTER_TEST(arguments, typed_inventory_count == 1458);
    BUSTER_TEST(arguments, standalone_inventory_count == 32);
    BUSTER_TEST(arguments, memcmp(typed_inventory_digest, expected_typed_inventory_digest, sizeof(typed_inventory_digest)) == 0);
    BUSTER_TEST(arguments, memcmp(standalone_inventory_digest, expected_standalone_inventory_digest,
                                  sizeof(standalone_inventory_digest)) == 0);
    BUSTER_TEST(arguments, memcmp(typed_inventory_hash_digest, expected_typed_inventory_hash_digest,
                                  sizeof(typed_inventory_hash_digest)) == 0);
    BUSTER_TEST(arguments, memcmp(standalone_inventory_hash_digest, expected_standalone_inventory_hash_digest,
                                  sizeof(standalone_inventory_hash_digest)) == 0);
    BUSTER_TEST(arguments, avx10_family_count == 130 && avx10_family_counts[0] == 36 && avx10_family_counts[1] == 18 &&
                             avx10_family_counts[2] == 44 && avx10_family_counts[3] == 32);
    BUSTER_TEST(arguments, memcmp(avx10_family_digest, expected_avx10_family_digest, sizeof(avx10_family_digest)) == 0);
    BUSTER_TEST(arguments, control_inventory_count == 74 && control_inventory_counts[0] == 36 &&
                             control_inventory_counts[1] == 26 && control_inventory_counts[2] == 8 &&
                             control_inventory_counts[3] == 4);
    BUSTER_TEST(arguments, memcmp(control_inventory_digest, expected_control_inventory_digest,
                                  sizeof(control_inventory_digest)) == 0);

    // Plain legacy XMM memory-source authority is an exhaustive metadata
    // topology, not a mnemonic or census-class list.  The inventory contains
    // every normalized MODRM/MEMORY/REGISTER form whose canonical query has
    // one XMM128 register destination and one ordinary memory source.  The
    // changed partition is pinned separately so already-capable stores,
    // policies, and malformed/ambiguous forms remain visible controls.
    u8 legacy_xmm_memory_inventory_hash_text[220 * 32] = {0};
    u8 legacy_xmm_memory_changed_hash_text[24 * 32] = {0};
    u32 legacy_xmm_memory_inventory_hash_length = 0;
    u32 legacy_xmm_memory_changed_hash_length = 0;
    u32 legacy_xmm_memory_inventory_count = 0;
    u32 legacy_xmm_memory_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT]
                                     [BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT] = {0};
    for (u32 legacy_xmm_form_id = 0; legacy_xmm_form_id < form_count; legacy_xmm_form_id += 1)
    {
        BusterX86MetadataForm legacy_xmm_form = {0};
        BusterX86MetadataPhysicalQuery legacy_xmm_query = {0};
        BusterX86MetadataPhysicalOperand legacy_xmm_operands[16] = {0};
        String8 legacy_xmm_features[1] = {0};
        char8 legacy_xmm_mnemonic[128] = {0};
        BusterX86MetadataFormKey legacy_xmm_key = {0};
        if (!buster_x86_metadata_form(legacy_xmm_form_id, &legacy_xmm_form) ||
            !buster_x86_completion_census_test_query(legacy_xmm_form_id, &legacy_xmm_query, legacy_xmm_operands,
                                                      legacy_xmm_features, legacy_xmm_mnemonic) ||
            !buster_x86_metadata_legacy_xmm_memory_authoritative(legacy_xmm_form, legacy_xmm_query))
            continue;
        bool legacy_xmm_key_ok = buster_x86_metadata_form_key(legacy_xmm_form_id, &legacy_xmm_key);
        BUSTER_TEST(arguments, legacy_xmm_key_ok);
        if (!legacy_xmm_key_ok) continue;
        String8 legacy_xmm_hash_line = string_format(arguments->arena,
                                                      S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                      legacy_xmm_form_id, legacy_xmm_key.stable_hash);
        bool legacy_xmm_hash_fits = legacy_xmm_memory_inventory_hash_length + legacy_xmm_hash_line.length <=
                                    sizeof(legacy_xmm_memory_inventory_hash_text);
        BUSTER_TEST(arguments, legacy_xmm_hash_fits);
        if (!legacy_xmm_hash_fits) continue;
        memcpy(legacy_xmm_memory_inventory_hash_text + legacy_xmm_memory_inventory_hash_length,
               legacy_xmm_hash_line.pointer, legacy_xmm_hash_line.length);
        legacy_xmm_memory_inventory_hash_length += (u32)legacy_xmm_hash_line.length;
        legacy_xmm_memory_inventory_count += 1;
        legacy_xmm_memory_pair_matrix[records[legacy_xmm_form_id].intel_class][records[legacy_xmm_form_id].att_class] += 1;
    }
    for (u32 legacy_xmm_changed_index = 0;
         legacy_xmm_changed_index < BUSTER_ARRAY_LENGTH(x86_completion_census_legacy_xmm_changed_forms);
         legacy_xmm_changed_index += 1)
    {
        X86CompletionCensusFormKey expected = x86_completion_census_legacy_xmm_changed_forms[legacy_xmm_changed_index];
        BusterX86MetadataForm legacy_xmm_form = {0};
        BusterX86MetadataPhysicalQuery legacy_xmm_query = {0};
        BusterX86MetadataPhysicalOperand legacy_xmm_operands[16] = {0};
        String8 legacy_xmm_features[1] = {0};
        char8 legacy_xmm_mnemonic[128] = {0};
        BusterX86MetadataFormKey legacy_xmm_key = {0};
        bool legacy_xmm_ok = buster_x86_metadata_form(expected.form_id, &legacy_xmm_form) &&
                             buster_x86_completion_census_test_query(expected.form_id, &legacy_xmm_query, legacy_xmm_operands,
                                                                       legacy_xmm_features, legacy_xmm_mnemonic) &&
                             buster_x86_metadata_form_key(expected.form_id, &legacy_xmm_key) &&
                             buster_x86_metadata_legacy_xmm_memory_authoritative(legacy_xmm_form, legacy_xmm_query);
        BUSTER_TEST(arguments, legacy_xmm_ok && legacy_xmm_key.stable_hash == expected.stable_hash &&
                                 records[expected.form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected.form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[expected.form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[expected.form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
        String8 legacy_xmm_hash_line = string_format(arguments->arena,
                                                      S8("{u32} {u64:x,width=[0,16],no_prefix}\n"),
                                                      expected.form_id, expected.stable_hash);
        bool legacy_xmm_hash_fits = legacy_xmm_memory_changed_hash_length + legacy_xmm_hash_line.length <=
                                    sizeof(legacy_xmm_memory_changed_hash_text);
        BUSTER_TEST(arguments, legacy_xmm_hash_fits);
        if (legacy_xmm_hash_fits)
        {
            memcpy(legacy_xmm_memory_changed_hash_text + legacy_xmm_memory_changed_hash_length,
                   legacy_xmm_hash_line.pointer, legacy_xmm_hash_line.length);
            legacy_xmm_memory_changed_hash_length += (u32)legacy_xmm_hash_line.length;
        }
    }
    for (u32 legacy_xmm_control_index = 0;
         legacy_xmm_control_index < BUSTER_ARRAY_LENGTH(x86_completion_census_legacy_xmm_controls);
         legacy_xmm_control_index += 1)
    {
        X86CompletionCensusOutcomeControl expected = x86_completion_census_legacy_xmm_controls[legacy_xmm_control_index];
        BusterX86MetadataFormKey legacy_xmm_key = {0};
        bool legacy_xmm_control_ok = buster_x86_metadata_form_key(expected.form_id, &legacy_xmm_key);
        BUSTER_TEST(arguments, legacy_xmm_control_ok && legacy_xmm_key.stable_hash == expected.stable_hash &&
                                 records[expected.form_id].intel_class == expected.intel_class &&
                                 records[expected.form_id].att_class == expected.att_class &&
                                 records[expected.form_id].intel_source_reason == expected.intel_reason &&
                                 records[expected.form_id].att_source_reason == expected.att_reason);
    }
    u8 legacy_xmm_memory_inventory_digest[32] = {0};
    u8 legacy_xmm_memory_changed_digest[32] = {0};
    link_sha256(arguments->arena, legacy_xmm_memory_inventory_hash_text, legacy_xmm_memory_inventory_hash_length,
                legacy_xmm_memory_inventory_digest);
    link_sha256(arguments->arena, legacy_xmm_memory_changed_hash_text, legacy_xmm_memory_changed_hash_length,
                legacy_xmm_memory_changed_digest);
    static u8 const expected_legacy_xmm_memory_inventory_digest[32] = {
        0x9c, 0x17, 0x9e, 0xad, 0x87, 0x4d, 0xed, 0x5a, 0x6e, 0x31, 0x60, 0x7b, 0xc8, 0x97, 0x71, 0x92,
        0xb5, 0xc1, 0xc0, 0x2f, 0xfa, 0x97, 0x90, 0x1c, 0x6d, 0xc6, 0x27, 0xed, 0x60, 0xee, 0xd6, 0x6a,
    };
    static u8 const expected_legacy_xmm_memory_changed_digest[32] = {
        0x3f, 0x68, 0xc3, 0x44, 0x7a, 0x97, 0xc6, 0x0a, 0xcb, 0x46, 0xa3, 0x0d, 0x8e, 0x61, 0x70, 0xe0,
        0x2f, 0x37, 0x3d, 0xfa, 0xb0, 0xf5, 0x64, 0xab, 0xff, 0x51, 0x5d, 0x03, 0x03, 0x97, 0x3c, 0x07,
    };
    BUSTER_TEST(arguments, legacy_xmm_memory_inventory_count == 195 && legacy_xmm_memory_inventory_hash_length > 0 &&
                             legacy_xmm_memory_changed_hash_length > 0 &&
                             memcmp(legacy_xmm_memory_inventory_digest, expected_legacy_xmm_memory_inventory_digest,
                                    sizeof(expected_legacy_xmm_memory_inventory_digest)) == 0 &&
                             memcmp(legacy_xmm_memory_changed_digest, expected_legacy_xmm_memory_changed_digest,
                                    sizeof(expected_legacy_xmm_memory_changed_digest)) == 0);
    BUSTER_TEST(arguments, legacy_xmm_memory_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                                  [BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT] == 139 &&
                             legacy_xmm_memory_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                                  [BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] == 51 &&
                             legacy_xmm_memory_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED]
                                  [BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] == 1 &&
                             legacy_xmm_memory_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]
                                  [BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED] == 4);

    // The VSIB source cohort is mechanically exhaustive: every normalized
    // form whose canonical physical query binds one VSIB memory operand is
    // classified from metadata topology and ISA family, then pinned by a
    // dense (form id, stable hash) digest.  No mnemonic or hand-maintained
    // row list participates in the inventory.
    u8 vsib_inventory_hash_text[80 * 64] = {0};
    u8 vsib_inventory_digest[32] = {0};
    u32 vsib_inventory_hash_length = 0;
    u32 vsib_total = 0;
    u32 vsib_evex_gather_count = 0;
    u32 vsib_evex_scatter_count = 0;
    u32 vsib_avx2_gather_count = 0;
    u32 vsib_prefetch_count = 0;
    u32 vsib_index_width_counts[3] = {0};
    u32 vsib_intel_exact_count = 0;
    u32 vsib_att_exact_count = 0;
    u32 vsib_intel_policy_count = 0;
    u32 vsib_att_policy_count = 0;
    for (u32 vsib_form_id = 0; vsib_form_id < form_count; vsib_form_id += 1)
    {
        BusterX86MetadataForm vsib_form = {0};
        BusterX86MetadataPhysicalQuery vsib_query = {0};
        BusterX86MetadataPhysicalOperand vsib_operands[16] = {0};
        String8 vsib_features[1] = {0};
        char8 vsib_mnemonic[128] = {0};
        BusterX86MetadataFormKey vsib_key = {0};
        u32 vsib_memory_index = UINT32_MAX;
        u32 vsib_memory_count = 0;
        u32 vsib_mask_count = 0;
        u32 vsib_mask_index = UINT32_MAX;
        bool form_ok = buster_x86_metadata_form(vsib_form_id, &vsib_form);
        bool vsib_form_flagged = form_ok && (vsib_form.field_flags & BUSTER_X86_METADATA_FIELD_VSIB) != 0;
        bool query_ok = form_ok && buster_x86_completion_census_test_query(vsib_form_id, &vsib_query, vsib_operands,
                                                                            vsib_features, vsib_mnemonic);
        BUSTER_TEST(arguments, !vsib_form_flagged || query_ok);
        if (!query_ok) continue;
        bool vsib_operand_capacity_ok = vsib_query.operand_count <= BUSTER_ARRAY_LENGTH(vsib_operands);
        BUSTER_TEST(arguments, vsib_operand_capacity_ok);
        if (!vsib_operand_capacity_ok) continue;
        u32 operand_index = 0;
        for (; operand_index < vsib_query.operand_count; operand_index += 1)
        {
            if (vsib_operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                vsib_operands[operand_index].memory.vsib)
            {
                vsib_memory_index = operand_index;
                vsib_memory_count += 1;
            }
            if (vsib_operands[operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                vsib_operands[operand_index].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK)
            {
                vsib_mask_count += 1;
                vsib_mask_index = operand_index;
            }
        }
        if (!vsib_form_flagged && !vsib_memory_count) continue;
        bool vsib_shape_ok = vsib_form_flagged && query_ok && vsib_memory_count == 1 &&
                             vsib_memory_index != UINT32_MAX && vsib_memory_index < vsib_query.operand_count;
        BUSTER_TEST(arguments, vsib_shape_ok);
        if (!vsib_shape_ok) continue;
        bool vsib_key_ok = buster_x86_metadata_form_key(vsib_form_id, &vsib_key);
        BUSTER_TEST(arguments, vsib_key_ok);
        if (!vsib_key_ok) continue;
        String8 vsib_hash_text = string_format(arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"),
                                                vsib_form_id, vsib_key.stable_hash);
        bool vsib_hash_fits = vsib_inventory_hash_length + vsib_hash_text.length <= sizeof(vsib_inventory_hash_text);
        BUSTER_TEST(arguments, vsib_hash_fits);
        if (!vsib_hash_fits) continue;
        memcpy(vsib_inventory_hash_text + vsib_inventory_hash_length, vsib_hash_text.pointer, vsib_hash_text.length);
        vsib_inventory_hash_length += (u32)vsib_hash_text.length;
        vsib_total += 1;
        BusterX86MetadataPhysicalOperand vsib_memory = vsib_operands[vsib_memory_index];
        BUSTER_TEST(arguments, vsib_memory.memory.has_index && vsib_memory.memory.vsib &&
                                 vsib_memory.memory.index.index == 1 &&
                                 (vsib_memory.memory.index.width == 128 || vsib_memory.memory.index.width == 256 ||
                                  vsib_memory.memory.index.width == 512));
        if (vsib_memory.memory.index.width == 128) vsib_index_width_counts[0] += 1;
        else if (vsib_memory.memory.index.width == 256) vsib_index_width_counts[1] += 1;
        else if (vsib_memory.memory.index.width == 512) vsib_index_width_counts[2] += 1;
        String8 vsib_category = buster_x86_metadata_string_span(vsib_form.category);
        String8 vsib_extension = buster_x86_metadata_string_span(vsib_form.extension);
        String8 vsib_isa_set = buster_x86_metadata_string_span(vsib_form.isa_set);
        bool vsib_prefetch = string_starts_with_sequence(vsib_isa_set, S8("AVX512PF_"));
        bool vsib_avx2 = string_equal(vsib_extension, S8("AVX2GATHER"));
        bool vsib_evex = vsib_form.encoder_family == BUSTER_X86_METADATA_ENCODER_EVEX;
        if (vsib_prefetch)
        {
            vsib_prefetch_count += 1;
            BUSTER_TEST(arguments, vsib_mask_count == 1 && vsib_memory_index == 0);
            BUSTER_TEST(arguments, records[vsib_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                     records[vsib_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE &&
                                     records[vsib_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                     records[vsib_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE);
            vsib_intel_policy_count += records[vsib_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
            vsib_att_policy_count += records[vsib_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
        }
        else
        {
            if (vsib_evex && string_equal(vsib_category, S8("GATHER"))) vsib_evex_gather_count += 1;
            else if (vsib_evex && string_equal(vsib_category, S8("SCATTER"))) vsib_evex_scatter_count += 1;
            else if (vsib_avx2 && string_equal(vsib_category, S8("AVX2GATHER"))) vsib_avx2_gather_count += 1;
            else BUSTER_TEST(arguments, false);
            BUSTER_TEST(arguments, records[vsib_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[vsib_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                     records[vsib_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                     records[vsib_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
            vsib_intel_exact_count += records[vsib_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
            vsib_att_exact_count += records[vsib_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
            if (vsib_evex) BUSTER_TEST(arguments, vsib_mask_count == 1 && vsib_mask_index != UINT32_MAX &&
                                      vsib_operands[vsib_mask_index].reg.index == 1);
            if (vsib_avx2) BUSTER_TEST(arguments, vsib_mask_count == 0);
        }
    }
    link_sha256(arguments->arena, vsib_inventory_hash_text, vsib_inventory_hash_length, vsib_inventory_digest);
    static u8 const expected_vsib_inventory_digest[32] = {
        0xbd, 0x12, 0x09, 0xab, 0x3a, 0x27, 0x9d, 0x42, 0xd0, 0xaf, 0xdf, 0xd6, 0xee, 0x3e, 0x99, 0x87,
        0xdb, 0xde, 0x9a, 0x99, 0x17, 0x1b, 0xc2, 0x69, 0x70, 0x3e, 0x76, 0x5a, 0x5c, 0xe4, 0x77, 0x42,
    };
    BUSTER_TEST(arguments, vsib_total == 80 && vsib_evex_gather_count == 24 && vsib_evex_scatter_count == 24 &&
                             vsib_avx2_gather_count == 16 && vsib_prefetch_count == 16);
    BUSTER_TEST(arguments, vsib_index_width_counts[0] == 30 && vsib_index_width_counts[1] == 26 &&
                             vsib_index_width_counts[2] == 24);
    BUSTER_TEST(arguments, vsib_intel_exact_count == 64 && vsib_att_exact_count == 64 &&
                             vsib_intel_policy_count == 16 && vsib_att_policy_count == 16);
    BUSTER_TEST(arguments, memcmp(vsib_inventory_digest, expected_vsib_inventory_digest,
                                  sizeof(vsib_inventory_digest)) == 0);

    // The legacy MMX read-memory cohort is mechanically exhaustive.  Its
    // source proof is the metadata topology: one visible slot-0 MMX64
    // READ|WRITE register, one visible slot-0 READ memory operand, and no
    // decorators or extension controls.  The AT&T source omits a scalar
    // suffix because the explicit MMX register supplies the aggregate 64-bit
    // memory width; no mnemonic or generated-form identity participates.
    u8 mmx_inventory_hash_text[55 * 64] = {0};
    u8 mmx_new_hash_text[37 * 64] = {0};
    u8 mmx_preexisting_hash_text[11 * 64] = {0};
    u8 mmx_control_hash_text[7 * 64] = {0};
    u32 mmx_inventory_hash_length = 0;
    u32 mmx_new_hash_length = 0;
    u32 mmx_preexisting_hash_length = 0;
    u32 mmx_control_hash_length = 0;
    u32 mmx_new_count = 0;
    u32 mmx_preexisting_count = 0;
    u32 mmx_control_count = 0;
    u32 mmx_inventory_count = 0;
    u32 mmx_exact_count = 0;
    u32 mmx_intel_invalid_count = 0;
    u32 mmx_feature_checked_count = 0;
    String8 mmx_empty_features[1] = {0};
    String8 mmx_sse2_features[] = {S8("sse2")};
    static u32 const mmx_intel_invalid_ids[] = {10195, 10197, 10199, 10221, 10223, 10225, 10331};
    static u32 const mmx_preexisting_ids[] = {10329, 10695, 10727, 10735, 10753, 10755,
                                               10757, 10759, 10761, 10763, 10765};
    u8 mmx_inventory_digest[32] = {0};
    for (u32 mmx_form_id = 0; mmx_form_id < form_count; mmx_form_id += 1)
    {
        BusterX86MetadataForm mmx_form = {0};
        BusterX86MetadataFormKey mmx_key = {0};
        BusterX86MetadataPhysicalQuery mmx_query = {0};
        BusterX86MetadataPhysicalOperand mmx_operands[16] = {0};
        String8 mmx_features[1] = {0};
        char8 mmx_mnemonic[128] = {0};
        if (!buster_x86_metadata_form(mmx_form_id, &mmx_form) ||
            mmx_form.coverage_class != BUSTER_X86_METADATA_COVERAGE_NORMALIZED ||
            mmx_form.encoder_family != BUSTER_X86_METADATA_ENCODER_LEGACY ||
            mmx_form.field_flags != (BUSTER_X86_METADATA_FIELD_MODRM | BUSTER_X86_METADATA_FIELD_MEMORY |
                                     BUSTER_X86_METADATA_FIELD_REGISTER) ||
            mmx_form.decorator_flags || mmx_form.apx_flags || mmx_form.amx_flags ||
            !buster_x86_completion_census_test_query(mmx_form_id, &mmx_query, mmx_operands, mmx_features, mmx_mnemonic))
            continue;
        String8 mmx_isa_set = buster_x86_metadata_string_span(mmx_form.isa_set);
        if (!(string_equal(mmx_isa_set, S8("PENTIUMMMX")) || string_equal(mmx_isa_set, S8("SSE2MMX")))) continue;
        u32 mmx_register_count = 0;
        u32 mmx_memory_count = 0;
        u32 mmx_visible_count = 0;
        for (u32 metadata_index = 0; metadata_index < mmx_form.operand_count; metadata_index += 1)
        {
            BusterX86MetadataOperand metadata = {0};
            BUSTER_TEST(arguments, buster_x86_metadata_operand(mmx_form_id, metadata_index, &metadata));
            if (!metadata.visible) continue;
            mmx_visible_count += 1;
            if (metadata.kind == BUSTER_X86_METADATA_OPERAND_REGISTER &&
                metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX && metadata.slot == 0 &&
                metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_REG &&
                metadata.access == (BUSTER_X86_METADATA_ACCESS_READ | BUSTER_X86_METADATA_ACCESS_WRITE))
            {
                mmx_register_count += 1;
            }
            else if (metadata.kind == BUSTER_X86_METADATA_OPERAND_MEMORY &&
                     metadata.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MEMORY && metadata.slot == 0 &&
                     metadata.field_source == BUSTER_X86_METADATA_FIELD_SOURCE_RM &&
                     metadata.access == BUSTER_X86_METADATA_ACCESS_READ)
            {
                mmx_memory_count += 1;
            }
        }
        if (mmx_visible_count != 2 || mmx_register_count != 1 || mmx_memory_count != 1 || mmx_query.operand_count != 2)
            continue;
        u32 mmx_register_index = UINT32_MAX;
        u32 mmx_memory_index = UINT32_MAX;
        for (u32 operand_index = 0; operand_index < mmx_query.operand_count; operand_index += 1)
        {
            BusterX86MetadataPhysicalOperand operand = mmx_operands[operand_index];
            if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                operand.reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MMX && operand.reg.width == 64)
                mmx_register_index = operand_index;
            else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY && !operand.memory.vsib &&
                     !operand.memory.has_segment)
                mmx_memory_index = operand_index;
        }
        bool mmx_physical_shape = mmx_register_index != UINT32_MAX && mmx_memory_index != UINT32_MAX;
        BUSTER_TEST(arguments, mmx_physical_shape);
        if (!mmx_physical_shape) continue;
        BUSTER_TEST(arguments, !buster_x86_metadata_test_feature_available(mmx_form_id, mmx_empty_features, 0));
        BUSTER_TEST(arguments, buster_x86_metadata_test_feature_available(mmx_form_id, mmx_sse2_features,
                                                                          BUSTER_ARRAY_LENGTH(mmx_sse2_features)));
        mmx_feature_checked_count += 1;
        BUSTER_TEST(arguments, buster_x86_metadata_form_key(mmx_form_id, &mmx_key));
        String8 mmx_hash_text = string_format(arguments->arena, S8("{u32} 0x{u64:x,width=[0,16],no_prefix}\n"),
                                               mmx_form_id, mmx_key.stable_hash);
        bool mmx_hash_fits = mmx_inventory_hash_length + mmx_hash_text.length <= sizeof(mmx_inventory_hash_text);
        BUSTER_TEST(arguments, mmx_hash_fits);
        if (!mmx_hash_fits) continue;
        memcpy(mmx_inventory_hash_text + mmx_inventory_hash_length, mmx_hash_text.pointer, mmx_hash_text.length);
        mmx_inventory_hash_length += (u32)mmx_hash_text.length;
        bool mmx_control_partition = false;
        bool mmx_preexisting_partition = false;
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(mmx_intel_invalid_ids); invalid_index += 1)
            mmx_control_partition |= mmx_form_id == mmx_intel_invalid_ids[invalid_index];
        for (u32 preexisting_index = 0; preexisting_index < BUSTER_ARRAY_LENGTH(mmx_preexisting_ids); preexisting_index += 1)
            mmx_preexisting_partition |= mmx_form_id == mmx_preexisting_ids[preexisting_index];
        if (mmx_control_partition)
        {
            bool hash_fits = mmx_control_hash_length + mmx_hash_text.length <= sizeof(mmx_control_hash_text);
            BUSTER_TEST(arguments, hash_fits);
            if (!hash_fits) continue;
            memcpy(mmx_control_hash_text + mmx_control_hash_length, mmx_hash_text.pointer, mmx_hash_text.length);
            mmx_control_hash_length += (u32)mmx_hash_text.length;
            mmx_control_count += 1;
        }
        else if (mmx_preexisting_partition)
        {
            bool hash_fits = mmx_preexisting_hash_length + mmx_hash_text.length <= sizeof(mmx_preexisting_hash_text);
            BUSTER_TEST(arguments, hash_fits);
            if (!hash_fits) continue;
            memcpy(mmx_preexisting_hash_text + mmx_preexisting_hash_length, mmx_hash_text.pointer, mmx_hash_text.length);
            mmx_preexisting_hash_length += (u32)mmx_hash_text.length;
            mmx_preexisting_count += 1;
        }
        else
        {
            bool hash_fits = mmx_new_hash_length + mmx_hash_text.length <= sizeof(mmx_new_hash_text);
            BUSTER_TEST(arguments, hash_fits);
            if (!hash_fits) continue;
            memcpy(mmx_new_hash_text + mmx_new_hash_length, mmx_hash_text.pointer, mmx_hash_text.length);
            mmx_new_hash_length += (u32)mmx_hash_text.length;
            mmx_new_count += 1;
        }
        mmx_inventory_count += 1;
        mmx_exact_count += records[mmx_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                           records[mmx_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                           records[mmx_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                           records[mmx_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE;
        mmx_intel_invalid_count += records[mmx_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED &&
                                   records[mmx_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS;
        bool mmx_intel_invalid_control = false;
        for (u32 invalid_index = 0; invalid_index < BUSTER_ARRAY_LENGTH(mmx_intel_invalid_ids); invalid_index += 1)
            mmx_intel_invalid_control |= mmx_form_id == mmx_intel_invalid_ids[invalid_index];
        bool mmx_exact_row = records[mmx_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                             records[mmx_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                             records[mmx_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                             records[mmx_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE;
        bool mmx_control_row = mmx_intel_invalid_control &&
                               records[mmx_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED &&
                               records[mmx_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS &&
                               records[mmx_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                               records[mmx_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE;
        BUSTER_TEST(arguments, mmx_exact_row || mmx_control_row);
    }
    link_sha256(arguments->arena, mmx_inventory_hash_text, mmx_inventory_hash_length, mmx_inventory_digest);
    u8 mmx_new_digest[32] = {0};
    u8 mmx_preexisting_digest[32] = {0};
    u8 mmx_control_digest[32] = {0};
    link_sha256(arguments->arena, mmx_new_hash_text, mmx_new_hash_length, mmx_new_digest);
    link_sha256(arguments->arena, mmx_preexisting_hash_text, mmx_preexisting_hash_length, mmx_preexisting_digest);
    link_sha256(arguments->arena, mmx_control_hash_text, mmx_control_hash_length, mmx_control_digest);
    static u8 const expected_mmx_inventory_digest[32] = {
        0xfc, 0x79, 0xb3, 0x9a, 0xc0, 0x42, 0xee, 0x80, 0xe4, 0x84, 0xc5, 0xae, 0x81, 0xcf, 0xba, 0x89,
        0x90, 0x0a, 0xa6, 0x8a, 0x00, 0x19, 0xb2, 0x3c, 0x93, 0x02, 0x48, 0x27, 0x28, 0x9d, 0x99, 0x8b,
    };
    static u8 const expected_mmx_new_digest[32] = {
        0x65, 0x9c, 0x9a, 0xa9, 0x55, 0xd2, 0x46, 0xf4, 0xa7, 0x94, 0x81, 0xdc, 0xba, 0xed, 0xee, 0x6d,
        0x78, 0xe4, 0xed, 0x55, 0xe2, 0xb8, 0x8b, 0x2e, 0xd1, 0x9a, 0x9e, 0xea, 0x93, 0x17, 0xc0, 0xab,
    };
    static u8 const expected_mmx_preexisting_digest[32] = {
        0xb7, 0xcd, 0x9f, 0x84, 0xd4, 0xee, 0x8b, 0xf0, 0x9a, 0x3e, 0x86, 0x63, 0xb8, 0x90, 0xe9, 0xee,
        0x01, 0xa9, 0xe9, 0xfa, 0x13, 0xfd, 0x5a, 0x9d, 0x1f, 0x53, 0x7a, 0x59, 0xff, 0x25, 0xe2, 0xf0,
    };
    static u8 const expected_mmx_control_digest[32] = {
        0x17, 0x1f, 0x4b, 0xdb, 0x28, 0xd0, 0x16, 0x84, 0x74, 0x61, 0xdc, 0x00, 0xb9, 0x28, 0xbb, 0xae,
        0xfd, 0x0b, 0xff, 0xc0, 0x99, 0x60, 0xdc, 0x97, 0x88, 0xf7, 0x07, 0x8d, 0x35, 0x06, 0x1d, 0xff,
    };
    BUSTER_TEST(arguments, mmx_inventory_count == 55 && mmx_exact_count == 48 && mmx_intel_invalid_count == 7);
    BUSTER_TEST(arguments, mmx_feature_checked_count == 55);
    BUSTER_TEST(arguments, memcmp(mmx_inventory_digest, expected_mmx_inventory_digest, sizeof(mmx_inventory_digest)) == 0);
    BUSTER_TEST(arguments, mmx_new_count == 37 && mmx_preexisting_count == 11 && mmx_control_count == 7 &&
                             mmx_new_hash_length && mmx_preexisting_hash_length && mmx_control_hash_length);
    BUSTER_TEST(arguments, memcmp(mmx_new_digest, expected_mmx_new_digest, sizeof(mmx_new_digest)) == 0 &&
                             memcmp(mmx_preexisting_digest, expected_mmx_preexisting_digest, sizeof(mmx_preexisting_digest)) == 0 &&
                             memcmp(mmx_control_digest, expected_mmx_control_digest, sizeof(mmx_control_digest)) == 0);
    // The semantic inventory contains 55 forms. Seven Intel-invalid rows
    // remain unchanged, eleven were already AT&T-capable through the
    // handwritten packed path, and the metadata seam truthfully unlocks the
    // remaining 37 without changing those rows.  The aggregate ledger below
    // also includes this PR's independent CPU-feature closure movement.
    for (u32 preexisting_index = 0; preexisting_index < BUSTER_ARRAY_LENGTH(mmx_preexisting_ids); preexisting_index += 1)
    {
        u32 form_id = mmx_preexisting_ids[preexisting_index];
        BUSTER_TEST(arguments, records[form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 records[form_id].intel_byte_count == records[form_id].att_byte_count);
    }

    // These six rows were already Intel-exact before the cohort landed. Keep
    // them explicit so the aggregate +68 Intel delta cannot hide a regression.
    static u32 const preexisting_intel_control_ids[] = {9483, 9484, 9487, 9488, 9833, 9836};
    for (u32 control_index = 0; control_index < BUSTER_ARRAY_LENGTH(preexisting_intel_control_ids); control_index += 1)
    {
        u32 form_id = preexisting_intel_control_ids[control_index];
        BUSTER_TEST(arguments, buster_x86_completion_census_test_control_prefix_shape(form_id) &&
                                 records[form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 records[form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }

    // Exercise the canonical metadata feature gate directly for every row in
    // the family cohort.  All four families require AVX10.2; only an ISA
    // suffix of *_512 additionally requires AVX10-512.
    String8 avx10_2_features[] = {S8("avx10.2")};
    String8 avx10_2_512_features[] = {S8("avx10.2"), S8("avx10-512")};
    String8 avx10_512_features[] = {S8("avx10-512")};
    u32 avx10_feature_checked_count = 0;
    for (u32 avx10_form_id = 0; avx10_form_id < form_count; avx10_form_id += 1)
    {
        if (!buster_x86_completion_census_test_decorator_shape(avx10_form_id, false)) continue;
        BusterX86MetadataForm avx10_form = {0};
        BUSTER_TEST(arguments, buster_x86_metadata_form(avx10_form_id, &avx10_form));
        String8 isa_set = buster_x86_metadata_string_span(avx10_form.isa_set);
        bool family_match = string_starts_with_sequence(isa_set, S8("AVX512_FP8_CONVERT_")) ||
                            string_starts_with_sequence(isa_set, S8("AVX512_MINMAX_")) ||
                            string_starts_with_sequence(isa_set, S8("AVX512_SAT_CVT_DS_")) ||
                            string_starts_with_sequence(isa_set, S8("AVX512_SAT_CVT_"));
        if (!family_match) continue;
        bool width512 = string_ends_with_sequence(isa_set, S8("_512"));
        BUSTER_TEST(arguments, buster_x86_metadata_test_feature_available(
                                 avx10_form_id, avx10_2_features, BUSTER_ARRAY_LENGTH(avx10_2_features)) == !width512);
        BUSTER_TEST(arguments, buster_x86_metadata_test_feature_available(
                                 avx10_form_id, avx10_2_512_features, BUSTER_ARRAY_LENGTH(avx10_2_512_features)));
        BUSTER_TEST(arguments, !buster_x86_metadata_test_feature_available(
                                  avx10_form_id, avx10_512_features, BUSTER_ARRAY_LENGTH(avx10_512_features)));
        avx10_feature_checked_count += 1;
    }
    BUSTER_TEST(arguments, avx10_feature_checked_count == 130);
    u32 typed_pair_total = 0;
    for (u32 intel_class = 0; intel_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; intel_class += 1)
        for (u32 att_class = 0; att_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; att_class += 1)
            typed_pair_total += typed_pair_matrix[intel_class][att_class];
    BUSTER_TEST(arguments, typed_pair_total == 1458);
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT] == 1433);
    static u32 const exact_intel_syntax_att_ids[] = {
        6336, 6338, 6374, 6376, 6502, 6506, 7669, 7673, 7715,
    };
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] ==
                         BUSTER_ARRAY_LENGTH(exact_intel_syntax_att_ids));
    for (u32 exact_syntax_index = 0; exact_syntax_index < BUSTER_ARRAY_LENGTH(exact_intel_syntax_att_ids); exact_syntax_index += 1)
    {
        BusterX86CompletionCensusRecord record = records[exact_intel_syntax_att_ids[exact_syntax_index]];
        BUSTER_TEST(arguments, record.form_id == exact_intel_syntax_att_ids[exact_syntax_index] &&
                                 record.intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 record.intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 record.att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED &&
                                 record.att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS);
    }
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED] == 0);
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED] == 16);
    for (u32 intel_class = 0; intel_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; intel_class += 1)
        for (u32 att_class = 0; att_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; att_class += 1)
            if (!((intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                   (att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
                    att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED ||
                    att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED)) ||
                  (intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                   att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED)))
                BUSTER_TEST(arguments, typed_pair_matrix[intel_class][att_class] == 0);

    // The three AVX512-VBMI2 VPSHLDQ broadcast forms are the exact
    // source-selection delta that exposed the AT&T suffix fallback seam.
    // Keep their stable metadata keys pinned independently of the aggregate
    // pair matrix so a future mnemonic/suffix change cannot silently move
    // them to the XOP alias family again.
    static u32 const vbmi2_suffix_form_ids[] = {8937, 8939, 8941};
    static u64 const vbmi2_suffix_form_hashes[] = {
        UINT64_C(9988580178933169345), UINT64_C(11535724125939013543), UINT64_C(5917272197917209722),
    };
    for (u32 vbmi2_index = 0; vbmi2_index < BUSTER_ARRAY_LENGTH(vbmi2_suffix_form_ids); vbmi2_index += 1)
    {
        BusterX86CompletionCensusRecord record = records[vbmi2_suffix_form_ids[vbmi2_index]];
        BUSTER_TEST(arguments, record.form_id == vbmi2_suffix_form_ids[vbmi2_index] &&
                                 record.stable_hash == vbmi2_suffix_form_hashes[vbmi2_index] &&
                                 record.intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 record.att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                                 record.intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE &&
                                 record.att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE);
    }

    static u32 const standalone_vcomx_ids[] = {4276, 4279, 4282, 4285, 4288, 4291};
    static u64 const standalone_vcomx_hashes[] = {
        UINT64_C(6982375845674161057), UINT64_C(16979018338420635722), UINT64_C(14724532497951551532),
        UINT64_C(4254777725290352627), UINT64_C(14277251828581801801), UINT64_C(17702768127218365195),
    };
    for (u32 standalone_index = 0; standalone_index < BUSTER_ARRAY_LENGTH(standalone_vcomx_ids); standalone_index += 1)
    {
        BusterX86CompletionCensusRecord record = records[standalone_vcomx_ids[standalone_index]];
        BUSTER_TEST(arguments, record.form_id == standalone_vcomx_ids[standalone_index] &&
                                 record.stable_hash == standalone_vcomx_hashes[standalone_index]);
        BUSTER_TEST(arguments, record.intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
                                 record.intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE);
        BUSTER_TEST(arguments, record.att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE &&
                                 record.att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_DECORATOR);
    }
    u32 standalone_non_vcomx_count = 0;
    u32 standalone_non_vcomx_intel_exact = 0;
    u32 standalone_non_vcomx_intel_policy = 0;
    u32 standalone_non_vcomx_att_decorator = 0;
    for (u32 inventory_form_id = 0; inventory_form_id < form_count; inventory_form_id += 1)
    {
        if (!buster_x86_completion_census_test_decorator_shape(inventory_form_id, true)) continue;
        bool is_vcomx = false;
        for (u32 standalone_index = 0; standalone_index < BUSTER_ARRAY_LENGTH(standalone_vcomx_ids); standalone_index += 1)
            is_vcomx |= inventory_form_id == standalone_vcomx_ids[standalone_index];
        if (is_vcomx) continue;
        standalone_non_vcomx_count += 1;
        standalone_non_vcomx_intel_exact += records[inventory_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT;
        standalone_non_vcomx_intel_policy += records[inventory_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
        standalone_non_vcomx_att_decorator +=
            records[inventory_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE &&
            records[inventory_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_DECORATOR;
    }
    BUSTER_TEST(arguments, standalone_non_vcomx_count == 26 && standalone_non_vcomx_intel_exact == 26 &&
                             standalone_non_vcomx_intel_policy == 0 && standalone_non_vcomx_att_decorator == 26);
    BUSTER_TEST(arguments, source.structural_complete && source.records_complete && source.diagnostics_complete);
    BUSTER_TEST(arguments, source.metadata_blocked_count == 0 && source.metadata_emitted_count == 10607);
    BUSTER_TEST(arguments, source.source_partition_expected_count == 10607);
    BUSTER_TEST(arguments, source.intel_source_partition_count == 10607 && source.att_source_partition_count == 10607);
    BUSTER_TEST(arguments, source.intel_attempted_count == 10607 && source.att_attempted_count == 10607);
    BUSTER_TEST(arguments, intel_reason_total == source.intel_attempted_count && att_reason_total == source.att_attempted_count);
    BUSTER_TEST(arguments, source.intel_exact_count == 5623 && source.intel_normalized_relocation_count == 28 &&
                             source.intel_alias_equivalent_count == 226 && source.intel_unresolved_count == 3962 &&
                             source.intel_byte_mismatch_count == 768 && source.intel_relocation_mismatch_count == 0 &&
                             source.intel_policy_rejected_count == 546 && source.intel_different_encoding_count == 17);
    BUSTER_TEST(arguments, source.att_exact_count == 5710 && source.att_normalized_relocation_count == 26 &&
                             source.att_alias_equivalent_count == 46 && source.att_unresolved_count == 3775 &&
                             source.att_byte_mismatch_count == 1050 && source.att_relocation_mismatch_count == 0 &&
                             source.att_policy_rejected_count == 555 && source.att_different_encoding_count == 17);
    BUSTER_TEST(arguments, intel_reason_non_none == source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE] +
                                             source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] +
                                             source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]);
    BUSTER_TEST(arguments, att_reason_non_none == source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE] +
                                           source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] +
                                           source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]);
    BUSTER_TEST(arguments, source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE] == 6662 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS] == 3263 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION] == 136 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_EXPRESSION] == 0 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE] == 546);
    BUSTER_TEST(arguments, source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE] == 6849 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_CONTROL] == 1915 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_MEMORY] == 4 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_DECORATOR] == 60 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS] == 1187 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION] == 37 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE] == 555);
    // Every baseline POLICY_FEATURE row must become byte-exact when the
    // target explicitly enables the complete x86 feature vocabulary.  This
    // is a proof of source reachability under an enabled target, not a change
    // to the default policy ledger.  Keep the loop keyed by the observed
    // reason so mode/privilege and parser classifications cannot be hidden by
    // an artificially broad target.
    Target feature_enabled_target = census_target;
    u32 intel_feature_policy_count = 0;
    u32 att_feature_policy_count = 0;
    u32 intel_feature_enabled_exact_count = 0;
    u32 att_feature_enabled_exact_count = 0;
    u32 intel_feature_still_policy_count = 0;
    u32 att_feature_still_policy_count = 0;
    // Diamond Rapids already carries the complete AVX10.2/AVX10-512 and
    // AVX512-VBMI2 vocabulary.  Add the only typed-policy family absent from
    // that public model (AVX512ER) while keeping the target internally valid;
    // synthesizing a bitset across the interleaved AArch64 enum values would
    // create an invalid cross-architecture target rather than a feature proof.
    feature_enabled_target.cpu_features =
        target_cpu_features_add(feature_enabled_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512ER);
    feature_enabled_target.cpu_features_explicit = true;
    for (u32 policy_form_id = 0; policy_form_id < form_count; policy_form_id += 1)
    {
        if (buster_x86_completion_census_test_decorator_shape(policy_form_id, false) &&
            records[policy_form_id].intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
            records[policy_form_id].intel_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE)
        {
            intel_feature_policy_count += 1;
            BusterX86CompletionCensusClass enabled_class =
                buster_x86_completion_census_test_source_class(arguments->arena, feature_enabled_target, policy_form_id, false);
            if (enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT)
                intel_feature_enabled_exact_count += 1;
            else
            {
                intel_feature_still_policy_count += enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
            }
            BUSTER_TEST(arguments, enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
                                     enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED);
        }
        if (buster_x86_completion_census_test_decorator_shape(policy_form_id, false) &&
            records[policy_form_id].att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED &&
            records[policy_form_id].att_source_reason == BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE)
        {
            att_feature_policy_count += 1;
            BusterX86CompletionCensusClass enabled_class =
                buster_x86_completion_census_test_source_class(arguments->arena, feature_enabled_target, policy_form_id, true);
            if (enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT)
                att_feature_enabled_exact_count += 1;
            else
            {
                att_feature_still_policy_count += enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED;
            }
            BUSTER_TEST(arguments, enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
                                     enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED);
        }
    }
    BUSTER_TEST(arguments, intel_feature_policy_count == 16 && att_feature_policy_count == 16 &&
                             intel_feature_enabled_exact_count == 16 && att_feature_enabled_exact_count == 16 &&
                             intel_feature_still_policy_count == 0 && att_feature_still_policy_count == 0);

    // The standalone-SAE inventory is a separate 32-row semantic cohort.
    // Keep its policy proof separate from the MASKmskw source-builder rows:
    // every baseline Intel feature-policy row is attempted under the
    // feature-enabled target.  The VCOMX rows remain policy-rejected because
    // their VIA/PadLock capability has no corresponding TargetCpuFeature;
    // that residual is recorded explicitly below rather than being omitted.
    u32 standalone_intel_feature_policy_count = 0;
    u32 standalone_intel_feature_exact_count = 0;
    u32 standalone_vcomx_enabled_nonexact_count = 0;
    u32 standalone_mover_enabled_nonexact_count = 0;
    for (u32 standalone_policy_form_id = 0; standalone_policy_form_id < form_count; standalone_policy_form_id += 1)
    {
        if (!buster_x86_completion_census_test_decorator_shape(standalone_policy_form_id, true) ||
            records[standalone_policy_form_id].intel_class != BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED ||
            records[standalone_policy_form_id].intel_source_reason != BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE)
            continue;
        standalone_intel_feature_policy_count += 1;
        BusterX86CompletionCensusClass enabled_class = buster_x86_completion_census_test_source_class(
            arguments->arena, feature_enabled_target, standalone_policy_form_id, false);
        if (enabled_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT)
            standalone_intel_feature_exact_count += 1;
        else
        {
            bool is_vcomx = false;
            for (u32 standalone_index = 0; standalone_index < BUSTER_ARRAY_LENGTH(standalone_vcomx_ids); standalone_index += 1)
                is_vcomx |= standalone_policy_form_id == standalone_vcomx_ids[standalone_index];
            if (is_vcomx) standalone_vcomx_enabled_nonexact_count += 1;
            else standalone_mover_enabled_nonexact_count += 1;
        }
    }
    BUSTER_TEST(arguments, standalone_intel_feature_policy_count == 6);
    BUSTER_TEST(arguments, standalone_intel_feature_exact_count + standalone_vcomx_enabled_nonexact_count +
                             standalone_mover_enabled_nonexact_count == standalone_intel_feature_policy_count &&
                             standalone_intel_feature_exact_count == 0 && standalone_vcomx_enabled_nonexact_count == 6 &&
                             standalone_mover_enabled_nonexact_count == 0);
    {
        typedef struct CensusReasonWitness CensusReasonWitness;
        struct CensusReasonWitness
        {
            u32 form_id;
            u64 stable_hash;
            u8 dialect;
            u8 expected_class;
            u8 expected_reason;
        };
        CensusReasonWitness const witnesses[] = {
            {0, UINT64_C(0xc07fcd97af562be9), 0, BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS},
            {326, UINT64_C(0x72aa28b9191feec9), 0, BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION},
            {4204, UINT64_C(0x4798dc8b1bc0ac94), 0, BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
            {7, UINT64_C(0xa1090cdb23861fbd), 0, BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE},
            {497, UINT64_C(0x6a04e34774497f64), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_CONTROL},
            {5507, UINT64_C(0x5bb4c576ce0a124f), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
            {3801, UINT64_C(0x142ac9c2f91af56c), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
            {0, UINT64_C(0xc07fcd97af562be9), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS},
            {326, UINT64_C(0x72aa28b9191feec9), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION},
            {7, UINT64_C(0xa1090cdb23861fbd), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE},
            {5584, UINT64_C(0xd19d549ad56300c4), 0, BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
            {5584, UINT64_C(0xd19d549ad56300c4), 1, BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
             BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE},
        };
        for (probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(witnesses); probe_index += 1)
        {
            CensusReasonWitness witness = witnesses[probe_index];
            BusterX86CompletionCensusRecord record = records[witness.form_id];
            BUSTER_TEST(arguments, record.form_id == witness.form_id && record.stable_hash == witness.stable_hash);
            if (witness.dialect == 0)
            {
                BUSTER_TEST(arguments, record.intel_class == witness.expected_class && record.intel_source_reason == witness.expected_reason);
            }
            else
                BUSTER_TEST(arguments, record.att_class == witness.expected_class && record.att_source_reason == witness.expected_reason);
        }
    }

    BUSTER_TEST(arguments, buster_x86_completion_census_test_query(511, &probe_query, probe_operands, probe_features,
                                                                    probe_mnemonic));
    BUSTER_TEST(arguments, probe_query.operand_count == 3);
    BUSTER_TEST(arguments, probe_operands[0].reg.index == 16 && probe_operands[1].reg.index == 17 &&
                             probe_operands[2].reg.index == 18);
    probe_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = probe_query, .form_id = 511, .output = probe_bytes, .output_capacity = sizeof(probe_bytes)});
    BUSTER_TEST(arguments, probe_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && probe_emit.byte_count == 6);

    memset(probe_operands, 0, sizeof(probe_operands));
    memset(probe_features, 0, sizeof(probe_features));
    memset(probe_mnemonic, 0, sizeof(probe_mnemonic));
    probe_query = (BusterX86MetadataPhysicalQuery){0};
    BUSTER_TEST(arguments, buster_x86_completion_census_test_query(529, &probe_query, probe_operands, probe_features,
                                                                    probe_mnemonic));
    for (probe_index = 0; probe_index < probe_query.operand_count; probe_index += 1)
    {
        if (probe_operands[probe_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE) break;
    }
    BUSTER_TEST(arguments, probe_index < probe_query.operand_count);
    BUSTER_TEST(arguments, probe_operands[probe_index].has_value && probe_operands[probe_index].value == 0x100);

    memset(probe_operands, 0, sizeof(probe_operands));
    memset(probe_features, 0, sizeof(probe_features));
    memset(probe_mnemonic, 0, sizeof(probe_mnemonic));
    probe_query = (BusterX86MetadataPhysicalQuery){0};
    BUSTER_TEST(arguments, buster_x86_completion_census_test_query(1436, &probe_query, probe_operands, probe_features,
                                                                    probe_mnemonic));
    for (probe_index = 0; probe_index < probe_query.operand_count; probe_index += 1)
    {
        if (probe_operands[probe_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) break;
    }
    BUSTER_TEST(arguments, probe_index < probe_query.operand_count);
    BUSTER_TEST(arguments, probe_operands[probe_index].memory.has_displacement &&
                             probe_operands[probe_index].memory.displacement == 1);
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 511, false);
    // ADC has two opcode-direction rows. Distinct APX roles make the public
    // spelling meaningful, but the strict census keeps form 511 as a byte
    // mismatch until a decoder-level semantic-equivalence proof exists.
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH);
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 529, false);
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
    memset(probe_operands, 0, sizeof(probe_operands));
    memset(probe_features, 0, sizeof(probe_features));
    memset(probe_mnemonic, 0, sizeof(probe_mnemonic));
    memset(probe_bytes, 0, sizeof(probe_bytes));
    probe_query = (BusterX86MetadataPhysicalQuery){0};
    BUSTER_TEST(arguments, buster_x86_completion_census_test_query(5584, &probe_query, probe_operands, probe_features,
                                                                    probe_mnemonic));
    BUSTER_TEST(arguments, probe_query.operand_count == 3 &&
                             probe_operands[0].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                             probe_operands[0].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM &&
                             probe_operands[0].reg.index == 0 &&
                             probe_operands[1].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER &&
                             probe_operands[1].reg.physical_class == BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK &&
                             probe_operands[1].reg.index == 0 &&
                             probe_operands[2].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY &&
                             probe_operands[2].memory.has_base && probe_operands[2].memory.base.index == 16 &&
                             probe_operands[2].memory.base.width == 64 && probe_operands[2].memory.source_width == 128);
    probe_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
        .physical = probe_query, .form_id = 5584, .output = probe_bytes, .output_capacity = sizeof(probe_bytes)});
    BUSTER_TEST(arguments, probe_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && probe_emit.byte_count == 6 &&
                             probe_bytes[0] == 0x62 && probe_bytes[1] == 0xf9 && probe_bytes[2] == 0x7d &&
                             probe_bytes[3] == 0x08 && probe_bytes[4] == 0x6f && probe_bytes[5] == 0x00);
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 5584, false);
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 5584, true);
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
    {
        AssemblyEncodeResult vfpclass_intel = assembly_encode(
            arguments->arena, S8("VFPCLASSBF16 k0, word ptr [rax]{1to8}, 0x0\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_INTEL});
        AssemblyEncodeResult vfpclass_att = assembly_encode(
            arguments->arena, S8("VFPCLASSBF16 $0x0, 0(%rax){1to8}, %k0\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        static u8 const expected_vfpclass_bytes[] = {0x62, 0xf3, 0x7f, 0x18, 0x66, 0x00, 0x00};
        BUSTER_TEST(arguments, vfpclass_intel.diagnostic_count == 0 && vfpclass_intel.bytes.length == sizeof(expected_vfpclass_bytes) &&
                                 memcmp(vfpclass_intel.bytes.pointer, expected_vfpclass_bytes, sizeof(expected_vfpclass_bytes)) == 0);
        BUSTER_TEST(arguments, vfpclass_att.diagnostic_count == 0 && vfpclass_att.bytes.length == sizeof(expected_vfpclass_bytes) &&
                                 memcmp(vfpclass_att.bytes.pointer, expected_vfpclass_bytes, sizeof(expected_vfpclass_bytes)) == 0);
        AssemblyEncodeResult vbmi_att = assembly_encode(
            arguments->arena, S8("VPSHLDQ $0x0, 0(%rax){1to2}, %xmm0, %xmm0\n"),
            (AssemblyEncodeOptions){.target = census_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        static u8 const expected_vbmi_att_bytes[] = {0x62, 0xf3, 0xfd, 0x18, 0x71, 0x00, 0x00};
        BUSTER_TEST(arguments, vbmi_att.diagnostic_count == 0 && vbmi_att.bytes.length == sizeof(expected_vbmi_att_bytes) &&
                                 memcmp(vbmi_att.bytes.pointer, expected_vbmi_att_bytes, sizeof(expected_vbmi_att_bytes)) == 0);

        // Keep the handwritten AMD-XOP mnemonic path alive while the
        // metadata selector handles the similarly suffixed VPSHLDQ form.
        // This ordinary unsuffixed XOP spelling must retain its historical
        // bytes and feature-gated selection.
        Target xop_target = census_target;
        xop_target.cpu_features = target_cpu_features_add(xop_target.cpu_features, TARGET_CPU_FEATURE_X86_XOP);
        xop_target.cpu_features_explicit = true;
        AssemblyEncodeResult xop_att = assembly_encode(
            arguments->arena, S8("vpshld %xmm2, %xmm1, %xmm0\n"),
            (AssemblyEncodeOptions){.target = xop_target, .syntax = ASSEMBLY_SYNTAX_ATT});
        static u8 const expected_xop_att_bytes[] = {0x8f, 0xe9, 0x68, 0x96, 0xc1};
        BUSTER_TEST(arguments, xop_att.diagnostic_count == 0 && xop_att.bytes.length == sizeof(expected_xop_att_bytes) &&
                                 memcmp(xop_att.bytes.pointer, expected_xop_att_bytes, sizeof(expected_xop_att_bytes)) == 0);
    }
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 1436, false);
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
    {
        // The shared authority predicate is deliberately topology-based.  A
        // valid ordinary-memory broadcast form is accepted, while each
        // excluded semantic shape is rejected independently.
        BusterX86MetadataForm authority_form = {0};
        BusterX86MetadataPhysicalOperand authority_operands[16] = {0};
        String8 authority_features[1] = {0};
        char8 authority_mnemonic[128] = {0};
        BusterX86MetadataPhysicalQuery authority_query = {0};
        BusterX86MetadataPhysicalOperand excluded_operands[16] = {0};
        u32 authority_memory_index = UINT32_MAX;
        BUSTER_TEST(arguments, buster_x86_metadata_form(3983, &authority_form) &&
                                 buster_x86_completion_census_test_query(3983, &authority_query, authority_operands,
                                                                          authority_features, authority_mnemonic));
        for (u32 authority_operand_index = 0; authority_operand_index < authority_query.operand_count; authority_operand_index += 1)
            if (authority_operands[authority_operand_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
            {
                authority_memory_index = authority_operand_index;
                break;
            }
        BUSTER_TEST(arguments, authority_memory_index != UINT32_MAX);
        BUSTER_TEST(arguments, buster_x86_metadata_typed_decorator_authoritative(authority_form, authority_query));
        BusterX86MetadataPhysicalQuery excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_query.address_size = 32;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_query.attributes.apx_flags = BUSTER_X86_METADATA_APX_NDD;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_query.attributes.has_dfv = true;
        excluded_query.attributes.dfv = 0;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_query.attributes.implicit_segment = BUSTER_X86_METADATA_SEGMENT_FS;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_operands[authority_memory_index].memory.vsib = true;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_operands[authority_memory_index].memory.has_segment = true;
        excluded_operands[authority_memory_index].memory.segment = BUSTER_X86_METADATA_SEGMENT_FS;
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_operands[0] = excluded_operands[authority_memory_index];
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));
        excluded_query = authority_query;
        memcpy(excluded_operands, authority_operands, sizeof(excluded_operands));
        excluded_query.operands = excluded_operands;
        excluded_query.operand_count += 1;
        excluded_operands[3] = excluded_operands[authority_memory_index];
        BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(authority_form, excluded_query));

        u32 standalone_authority_form_id = UINT32_MAX;
        BusterX86MetadataForm standalone_authority_form = {0};
        BusterX86MetadataPhysicalOperand standalone_authority_operands[16] = {0};
        String8 standalone_authority_features[1] = {0};
        char8 standalone_authority_mnemonic[128] = {0};
        BusterX86MetadataPhysicalQuery standalone_authority_query = {0};
        for (u32 candidate_id = 0; candidate_id < form_count; candidate_id += 1)
        {
            if (!buster_x86_completion_census_test_decorator_shape(candidate_id, true)) continue;
            standalone_authority_form_id = candidate_id;
            BUSTER_TEST(arguments, buster_x86_metadata_form(candidate_id, &standalone_authority_form) &&
                                     buster_x86_completion_census_test_query(candidate_id, &standalone_authority_query,
                                                                              standalone_authority_operands,
                                                                              standalone_authority_features,
                                                                              standalone_authority_mnemonic));
            break;
        }
        BUSTER_TEST(arguments, standalone_authority_form_id != UINT32_MAX);
        if (standalone_authority_form_id != UINT32_MAX)
        {
            BUSTER_TEST(arguments, buster_x86_metadata_typed_decorator_authoritative(standalone_authority_form,
                                                                                        standalone_authority_query));
            excluded_query = standalone_authority_query;
            memcpy(excluded_operands, standalone_authority_operands, sizeof(excluded_operands));
            excluded_query.operands = excluded_operands;
            excluded_query.operand_count += 1;
            excluded_operands[excluded_query.operand_count - 1] = excluded_operands[0];
            excluded_operands[excluded_query.operand_count - 1].kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY;
            BUSTER_TEST(arguments, !buster_x86_metadata_typed_decorator_authoritative(standalone_authority_form,
                                                                                         excluded_query));
        }
    }
    {
        u32 const apx_amx_form_ids[] = {490, 491, 492, 493, 494};
        u8 const apx_amx_opcode_prefixes[] = {0x7f, 0x7f, 0x7d, 0x7d, 0x7e};
        u8 const apx_amx_opcodes[] = {0x4b, 0x4a, 0x4a, 0x4b, 0x4b};
        for (probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(apx_amx_form_ids); probe_index += 1)
        {
            probe_form_id = apx_amx_form_ids[probe_index];
            memset(probe_operands, 0, sizeof(probe_operands));
            memset(probe_features, 0, sizeof(probe_features));
            memset(probe_mnemonic, 0, sizeof(probe_mnemonic));
            memset(probe_bytes, 0, sizeof(probe_bytes));
            probe_query = (BusterX86MetadataPhysicalQuery){0};
            BUSTER_TEST(arguments, buster_x86_completion_census_test_query(
                                       probe_form_id, &probe_query, probe_operands, probe_features, probe_mnemonic));
            for (class_index = 0; class_index < probe_query.operand_count; class_index += 1)
                if (probe_operands[class_index].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY) break;
            BUSTER_TEST(arguments, class_index < probe_query.operand_count &&
                                     probe_operands[class_index].memory.has_base &&
                                     probe_operands[class_index].memory.base.index == 16 &&
                                     probe_operands[class_index].memory.base.width == 64);
            probe_emit = buster_x86_metadata_emit_form((BusterX86MetadataEmitQuery){
                .physical = probe_query, .form_id = probe_form_id, .output = probe_bytes, .output_capacity = sizeof(probe_bytes)});
            BUSTER_TEST(arguments, probe_emit.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && probe_emit.byte_count == 7 &&
                                     probe_bytes[0] == 0x62 && probe_bytes[1] == 0xfa &&
                                     probe_bytes[2] == apx_amx_opcode_prefixes[probe_index] && probe_bytes[3] == 0x08 &&
                                     probe_bytes[4] == apx_amx_opcodes[probe_index] &&
                                     probe_bytes[5] == 0x04 && probe_bytes[6] == 0x20);
            probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, probe_form_id, false);
            BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
            probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, probe_form_id, true);
            BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
        }
    }
    for (probe_index = 0; probe_index < 4; probe_index += 1)
    {
        probe_form_id = 9891 + probe_index;
        memset(probe_operands, 0, sizeof(probe_operands));
        memset(probe_features, 0, sizeof(probe_features));
        memset(probe_mnemonic, 0, sizeof(probe_mnemonic));
        probe_query = (BusterX86MetadataPhysicalQuery){0};
        BUSTER_TEST(arguments, buster_x86_completion_census_test_query(probe_form_id, &probe_query, probe_operands, probe_features,
                                                                        probe_mnemonic));
        BUSTER_TEST(arguments, probe_query.operand_count == 1 &&
                                 probe_operands[0].kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY);
        BUSTER_TEST(arguments, probe_operands[0].memory.has_displacement &&
                                 probe_operands[0].memory.displacement == INT64_C(0x1122334455667788));
        probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, probe_form_id, false);
        BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
    }
#else
    BUSTER_TEST(arguments, true);
#endif
    return result;
}

#endif
