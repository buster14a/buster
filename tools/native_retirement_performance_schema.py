"""Shared schema for the production native-retirement census report.

The consumer checks this exact producer field set. A source-level test compares
it with validate_shards so production schema changes cannot go unnoticed. Unknown
fields still fail closed; adding a producer field therefore requires a reviewed
schema update instead of silently weakening a consumer.
"""

APPLICABILITY_CLASSES = (
    "admitted-supported",
    "retained-control",
    "retained-reference",
    "platform-inapplicable",
    "unavailable",
)

APPLICABILITY_FIELDS = (
    "row", "group", "fixture", "target", "cpu", "frontend", "allocator", "PIC",
    "applicability", "admission", "disposition", "reason", "ownership",
    "candidate_failure", "reference_failure", "acceptance_failure",
)

APPLICABILITY_SKIP_FIELDS = (
    "row", "group", "fixture", "target", "allocator", "applicability", "reason",
)

VALIDATOR_REPORT_FIELDS = (
    "schema",
    "directories",
    "shards",
    "profile",
    "rows_validated",
    "groups",
    "compiler_revision_claim",
    "baseline_revision_claim",
    "compiler_sha256",
    "baseline_sha256",
    "support_contract_sha256",
    "supported_gap_ledger_sha256",
    "applicability_ledger_sha256",
    "applicability_ledger_entries",
    "resource_include_sha256",
    "manifest_identity_sha256",
    "rows_identity_fields",
    "rows_identity_sha256",
    "input_ledger_fields",
    "input_ledger_sha256",
    "baseline_dispositions",
    "reference_dispositions",
    "setup_dispositions",
    "candidate_dispositions",
    "applicability_classes",
    "admission_classes",
    "applicability_counts",
    "admission_counts",
    "applicability_rows_by_class",
    "admission_rows_by_class",
    "supported_gap_rows",
    "supported_gap_count",
    "supported_gap_sha256",
    "applicability_rows",
    "applicability_evidence",
    "applicability_tsv",
    "applicability_sha256",
    "applicability_skip_rows",
    "applicability_skip_evidence",
    "residual_evidence",
    "residual_tsv",
    "residual_sha256",
    "residual_rows",
    "residual_limit",
    "residual_truncated",
    "candidate_failure_rows",
    "direct_reference_failure_rows",
    "reference_supplement_sha256",
    "reference_failure_rows",
    "acceptance_failure_rows",
    "inapplicable_rows",
    "fallback_defect_rows",
    "telemetry_defect_rows",
    "execution_defect_rows",
    "artifact_defect_rows",
    "unexpected_failure_rows",
    "require_clean_candidate",
    "require_clean_acceptance",
    "clean_candidate",
    "clean_acceptance",
    "complete_row_partition",
    "global_identity_unique",
)

PATH_FIELDS = (
    "directories",
    "applicability_evidence",
    "applicability_tsv",
    "applicability_skip_evidence",
    "residual_evidence",
    "residual_tsv",
)
