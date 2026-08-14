#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/target.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>

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
    probe_class = buster_x86_completion_census_test_source_class(arguments->arena, census_target, 1436, false);
    BUSTER_TEST(arguments, probe_class == BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT);
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
