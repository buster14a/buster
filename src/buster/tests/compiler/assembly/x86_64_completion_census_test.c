#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/target.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>

UnitTestResult x86_64_completion_census_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_CPU_ARCH_X86_64
    u32 form_count = buster_x86_metadata_form_count();
    BusterX86MetadataCoverageLedgerEntry* entries = arena_allocate(arguments->arena, BusterX86MetadataCoverageLedgerEntry, form_count);
    BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(entries, form_count);
    BUSTER_TEST(arguments, audit.complete);
    BUSTER_TEST(arguments, audit.entry_count == form_count);
    BUSTER_TEST(arguments, audit.normalized_entry_count == buster_x86_metadata_normalized_form_count());
    BUSTER_TEST(arguments, audit.emitted_count == 10606 && audit.blocked_count == 407);
    BUSTER_TEST(arguments, buster_x86_metadata_coverage_digest(entries, audit.entry_count, form_count) == UINT64_C(0xbebc4833a78c441c));
#else
    BUSTER_TEST(arguments, true);
#endif
    return result;
}

#endif
