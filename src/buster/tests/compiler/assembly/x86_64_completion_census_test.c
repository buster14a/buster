#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/target.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>

UnitTestResult x86_64_completion_census_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_CPU_ARCH_X86_64
    u32 form_count = buster_x86_metadata_form_count();
    u32 class_index = 0;
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
    BUSTER_TEST(arguments, audit.emitted_count == 10606 && audit.blocked_count == 407);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_digest(entries, audit.entry_count, form_count) == UINT64_C(0xbebc4833a78c441c));
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
#else
    BUSTER_TEST(arguments, true);
#endif
    return result;
}

#endif
