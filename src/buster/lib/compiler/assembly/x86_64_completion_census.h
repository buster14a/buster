#pragma once

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>

// This API is intentionally a report-producing core rather than a command or
// manifest writer.  Callers own the record/diagnostic storage and can decide
// how (or whether) to persist the returned result.

enum
{
    BUSTER_X86_COMPLETION_CENSUS_EXPECTED_FORM_COUNT = 11013,
    BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NORMALIZED_COUNT = 10607,
    BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_EMITTED_COUNT = 10607,
    BUSTER_X86_COMPLETION_CENSUS_EXPECTED_METADATA_BLOCKED_COUNT = 0,
    BUSTER_X86_COMPLETION_CENSUS_EXPECTED_NON_NORMALIZED_COUNT = 406,
};

typedef enum BusterX86CompletionCensusClass
{
    BUSTER_X86_COMPLETION_CENSUS_NOT_ATTEMPTED,
    BUSTER_X86_COMPLETION_CENSUS_STRUCTURAL_ONLY,
    BUSTER_X86_COMPLETION_CENSUS_POLICY_EXCLUDED,
    BUSTER_X86_COMPLETION_CENSUS_CANONICAL_QUERY_UNREPRESENTABLE,
    BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMIT_FAILURE,
    BUSTER_X86_COMPLETION_CENSUS_DIRECT_EMITTED,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_UNREPRESENTABLE,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_SYNTAX_REJECTED,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_POLICY_REJECTED,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_EXACT,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_NORMALIZED_RELOCATION,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_ALIAS_EQUIVALENT,
    // The public assembler accepted the synthesized spelling but selected a
    // different encoding shape. Keep this separate from a proven mismatch:
    // a mnemonic can have multiple legal metadata forms and assembly_encode
    // is allowed to choose one other than the canonical row under audit.
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_DIFFERENT_ENCODING,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_BYTE_MISMATCH,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_RELOCATION_MISMATCH,
    BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT,
} BusterX86CompletionCensusClass;

// Source construction and parser failures are tracked separately from the
// public reachability class.  The phase is part of the value so a reason is
// mutually exclusive and can be counted without a second parallel array.
// NONE is used for exact, relocation, alias, mismatch, and other rows whose
// source failure taxonomy does not apply.
typedef enum BusterX86CompletionCensusSourceReason
{
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_NONE,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_OPERAND,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_CONTROL,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_MEMORY,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_DECORATOR,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_IMPLICIT,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_FEATURE,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_MODE,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_CONSTRUCTION_OTHER,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_OPERANDS,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_UNKNOWN_INSTRUCTION,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_INVALID_EXPRESSION,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_SYNTAX_DIAGNOSTIC_OTHER,
    // Kept separate from parser syntax failures so the existing
    // SOURCE_POLICY_REJECTED class remains unchanged while feature-gated
    // rows are still visible in the reason ledger.
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_POLICY_FEATURE,
    BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT,
} BusterX86CompletionCensusSourceReason;

BUSTER_F_DECL String8 buster_x86_completion_census_source_reason_name(BusterX86CompletionCensusSourceReason reason);

typedef struct BusterX86CompletionCensusDiagnostic BusterX86CompletionCensusDiagnostic;
struct BusterX86CompletionCensusDiagnostic
{
    u32 form_id;
    u64 stable_hash;
    u8 dialect;
    u8 classification;
    u8 reason;
    u8 reserved0;
    u16 assembly_diagnostic_kind;
    u32 mismatch_index;
    u8 direct_byte;
    u8 source_byte;
    u16 reserved1;
};

typedef struct BusterX86CompletionCensusRecord BusterX86CompletionCensusRecord;
struct BusterX86CompletionCensusRecord
{
    // Structural identity and generated classification.
    u32 form_id;
    u64 stable_hash;
    u8 coverage_class;
    u8 encoder_family;
    u8 test_class;
    u8 structural_class;
    bool policy_excluded;
    bool canonical_query;
    bool metadata_emitted;
    bool reserved0;
    // Direct metadata result.  Relocations are summarized here; detailed
    // relocation values remain in the metadata emitter's caller-owned result.
    u32 metadata_byte_count;
    u32 metadata_relocation_count;
    u16 metadata_status;
    u16 canonical_operand_count;
    // Intel and AT&T source reachability are independent.  A source class is
    // never inferred from the other dialect or from a direct byte match.
    u8 intel_class;
    u8 att_class;
    u8 intel_capable;
    u8 att_capable;
    u32 intel_byte_count;
    u32 intel_relocation_count;
    u32 att_byte_count;
    u32 att_relocation_count;
    u16 intel_diagnostic_kind;
    u16 att_diagnostic_kind;
    u32 intel_mismatch_index;
    u32 att_mismatch_index;
    u8 intel_source_reason;
    u8 att_source_reason;
    u16 reserved1;
};

typedef struct BusterX86CompletionCensusQuery BusterX86CompletionCensusQuery;
struct BusterX86CompletionCensusQuery
{
    Arena* arena;
    Target target;
    BusterX86CompletionCensusRecord* records;
    u32 record_capacity;
    BusterX86CompletionCensusDiagnostic* diagnostics;
    u32 diagnostic_capacity;
    // Set these flags to select one or both source dialects. When both are
    // false the implementation runs both dialects. Policy rows are always
    // reported as excluded; this API does not synthesize them.
    bool run_intel;
    bool run_att;
    // Structural-only mode still performs the canonical query and direct
    // metadata emission, but deliberately skips all Intel/AT&T source
    // synthesis and assembly checks.  In this mode the source partition
    // fields in the result are zero by contract.
    bool structural_only;
    u8 reserved[5];
};

typedef struct BusterX86CompletionCensusResult BusterX86CompletionCensusResult;
struct BusterX86CompletionCensusResult
{
    // Structural completion means every generated row was scanned and every
    // requested record was materialized. It does not imply source coverage;
    // inspect intel_all_passed/att_all_passed and the class arrays for that.
    bool structural_complete;
    bool records_complete;
    bool intel_all_passed;
    bool att_all_passed;
    u32 required_form_count;
    u32 scanned_form_count;
    u32 record_count;
    u32 normalized_form_count;
    u32 policy_excluded_count;
    u32 non_normalized_form_count;
    u32 metadata_emitted_count;
    u32 metadata_emit_failed_count;
    u32 canonical_query_failed_count;
    u32 metadata_blocked_count;
    // These three fields are zero when the query is structural-only.  For a
    // source-enabled query, each dialect partition is measured against the
    // emitted metadata rows.
    u32 source_partition_expected_count;
    u32 intel_source_partition_count;
    u32 att_source_partition_count;
    bool form_partition_complete;
    bool normalized_partition_complete;
    bool metadata_partition_complete;
    u8 reserved1;
    u32 intel_attempted_count;
    u32 intel_exact_count;
    u32 intel_normalized_relocation_count;
    u32 intel_alias_equivalent_count;
    u32 intel_policy_rejected_count;
    u32 intel_different_encoding_count;
    u32 intel_byte_mismatch_count;
    u32 intel_relocation_mismatch_count;
    // Unresolved is deliberately limited to source-construction, assembler
    // rejection, policy rejection, and a public spelling that selected a
    // different legal encoding. Alias-equivalent rows are resolved; direct
    // metadata blockers never enter this denominator. Byte/relocation
    // mismatches are tracked separately and remain outside unresolved.
    u32 intel_unresolved_count;
    u32 att_attempted_count;
    u32 att_exact_count;
    u32 att_normalized_relocation_count;
    u32 att_alias_equivalent_count;
    u32 att_policy_rejected_count;
    u32 att_different_encoding_count;
    u32 att_byte_mismatch_count;
    u32 att_relocation_mismatch_count;
    u32 att_unresolved_count;
    u32 class_counts[BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT];
    u32 intel_class_counts[BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT];
    u32 att_class_counts[BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT];
    u32 intel_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT];
    u32 att_source_reason_counts[BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT];
    u32 diagnostic_count;
    u32 diagnostic_dropped_count;
    bool diagnostics_complete;
    u8 reserved0[3];
};

// Scans the immutable generated snapshot in form order.  No global report or
// file is written.  `records` and `diagnostics` may be null when the caller
// only needs aggregate counts. `structural_complete` requires either a
// caller-owned record array large enough for all forms or a null record pointer
// (aggregate-only mode). Diagnostics are optional bounded examples: a null or
// undersized diagnostic array never makes structural_complete false; inspect
// diagnostics_complete to know whether every diagnostic example fit.
BUSTER_F_DECL BusterX86CompletionCensusResult buster_x86_completion_census_run(BusterX86CompletionCensusQuery query);

#if BUSTER_INCLUDE_TESTS
// Exposes the census's deterministic source-query normalization without
// running the all-form source census. The returned storage is caller-owned.
BUSTER_F_DECL bool buster_x86_completion_census_test_query(u32 form_id, BusterX86MetadataPhysicalQuery* query,
                                                            BusterX86MetadataPhysicalOperand operands[16],
                                                            String8 features[1], char8 mnemonic_buffer[128]);
BUSTER_F_DECL BusterX86CompletionCensusClass buster_x86_completion_census_test_source_class(Arena* arena, Target target,
                                                                                             u32 form_id, bool att);
#endif
