#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#if !BUSTER_SINGLE_THREADED
#include <buster/lib/os.h>
#endif

#if BUSTER_INCLUDE_TESTS
enum
{
    BUSTER_X86_COMPLETION_COHORT_ALL,
    BUSTER_X86_COMPLETION_COHORT_NORMALIZED,
    BUSTER_X86_COMPLETION_COHORT_EMITTED,
    BUSTER_X86_COMPLETION_COHORT_BLOCKED,
    BUSTER_X86_COMPLETION_COHORT_COUNT,
    BUSTER_X86_COMPLETION_MAX_VISIBLE_OPERANDS = BUSTER_X86_METADATA_MAX_OPERAND_SHAPE,
};

typedef struct BusterX86CompletionLedger BusterX86CompletionLedger;
struct BusterX86CompletionLedger
{
    u32 form_count;
    u32 normalized_count;
    u32 emitted_count;
    u32 blocked_count;
    u32 operand_count;
    u32 operand_kind_counts[BUSTER_X86_METADATA_OPERAND_KIND_COUNT];
    u32 visible_operand_kind_counts[BUSTER_X86_METADATA_OPERAND_KIND_COUNT];
    u32 visible_count_distribution[BUSTER_X86_COMPLETION_MAX_VISIBLE_OPERANDS + 1];
    u32 field_cohorts[BUSTER_X86_COMPLETION_COHORT_COUNT][9];
    u32 decorator_cohorts[BUSTER_X86_COMPLETION_COHORT_COUNT][5];
    u32 apx_cohorts[BUSTER_X86_COMPLETION_COHORT_COUNT][6];
    u32 amx_cohorts[BUSTER_X86_COMPLETION_COHORT_COUNT][4];
    u32 family_all_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_all_emitted_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_all_blocked_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_emitted_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 family_blocked_counts[BUSTER_X86_METADATA_ENCODER_COUNT];
    u32 blocker_counts[BUSTER_X86_METADATA_COVERAGE_BLOCKER_COUNT];
    u32 duplicate_form_id_count;
    u32 duplicate_stable_hash_count;
    u32 zero_stable_hash_count;
    u32 emitted_nonzero_blocker_count;
    u64 digest;
};

BUSTER_F_DECL UnitTestResult x86_64_metadata_tests(UnitTestArguments* arguments);
#endif
