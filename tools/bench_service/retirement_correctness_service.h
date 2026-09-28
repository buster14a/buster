/* Private #1018 -> #1020 staged projection check. In addition to pinned #508
 * support, source applicability ledger, inputs, rows, manifest, and schema-2
 * validator report/applicability/skip sidecars, compiled profile pins bind each
 * file. The service recomputes raw identities and verifies the exact source-
 * derived skip set. It deliberately returns a failure before begin because
 * per-row configuration, #509 checks, oracle evidence, and command plans are
 * not independently authenticated. The blocked production profile lacks these
 * pins and fails closed earlier. The reference-policy importer below has the
 * same property for its reference-template/-inventory pins.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#include "retirement_binaries.h"
#include "retirement_matched_build.h"
#include "retirement_correctness.h"
#include "retirement_reference_template.h"

/* #1020 installed reference policy: decoded template and inventory plus the
 * held bin/clang. plan.template and plan.rows point into this object, so it
 * must stay in place until release. Zero-initialize before import. */
typedef struct BqRetirementReferencePolicy
{
    BqRetirementOracleTemplate template;
    BqRetirementReferencePlan plan;
    BqRetirementReferenceSourceIdentity source[2];
    BqRetirementOracleTemplateRow* template_rows;
    BqRetirementReferencePlanRow* plan_rows;
    char* text;
    char template_sha256[SHA256_HEX_CAPACITY], inventory_sha256[SHA256_HEX_CAPACITY];
    char toolchain_manifest_sha256[SHA256_HEX_CAPACITY];
    int clang;
    u32 owned;
} BqRetirementReferencePolicy;

/* Uses the compiled native-retirement profile. The blocked profile has no
 * reference-template/-inventory pins, so this fails closed until reviewed
 * pins exist. It supplies no #508 configuration or #509 authority. */
BUSTER_F_DECL BqError bq_retirement_reference_policy_import(int installed,
    BqRetirementPreparation const* preparation, BqRetirementToolchain const* toolchain,
    BqRetirementReferencePolicy* policy);
BUSTER_F_DECL bool bq_retirement_reference_policy_release(BqRetirementReferencePolicy* policy);

BUSTER_F_DECL BqError bq_retirement_correctness_begin_service(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, int support_declaration, int source_applicability_ledger,
    int census_inputs, int census_rows,
    int census_manifest, int validator_report, int validator_applicability, int validator_skips,
    String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate);
#endif
