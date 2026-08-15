#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/target.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/tests/compiler/link/link_test.h>

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
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_digest(entries, audit.entry_count, form_count) == UINT64_C(0xef8690567887ffc6));
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
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT] == 1442);
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED] == 0);
    BUSTER_TEST(arguments, typed_pair_matrix[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]
                             [BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED] == 16);
    for (u32 intel_class = 0; intel_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; intel_class += 1)
        for (u32 att_class = 0; att_class < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; att_class += 1)
            if (!((intel_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT &&
                   (att_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT ||
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
    BUSTER_TEST(arguments, source.intel_exact_count == 5242 && source.intel_normalized_relocation_count == 28 &&
                             source.intel_alias_equivalent_count == 190 && source.intel_unresolved_count == 4297 &&
                             source.intel_byte_mismatch_count == 850 && source.intel_relocation_mismatch_count == 0 &&
                             source.intel_policy_rejected_count == 594 && source.intel_different_encoding_count == 17);
    BUSTER_TEST(arguments, source.att_exact_count == 5676 && source.att_normalized_relocation_count == 26 &&
                             source.att_alias_equivalent_count == 42 && source.att_unresolved_count == 3931 &&
                             source.att_byte_mismatch_count == 932 && source.att_relocation_mismatch_count == 0 &&
                             source.att_policy_rejected_count == 603 && source.att_different_encoding_count == 17);
    BUSTER_TEST(arguments, intel_reason_non_none == source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE] +
                                             source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] +
                                             source.intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]);
    BUSTER_TEST(arguments, att_reason_non_none == source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE] +
                                           source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED] +
                                           source.att_class_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED]);
    BUSTER_TEST(arguments, source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE] == 6327 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS] == 3550 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION] == 136 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_EXPRESSION] == 0 &&
                             source.intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE] == 594);
    BUSTER_TEST(arguments, source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE] == 6693 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_CONTROL] == 1915 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_MEMORY] == 4 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_DECORATOR] == 60 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS] == 1295 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION] == 37 &&
                             source.att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE] == 603);

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
