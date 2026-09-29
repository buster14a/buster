/* Private #1018 -> #1020 staged projection check. In addition to pinned #508
 * support, source applicability ledger, inputs, rows, manifest, and schema-2
 * validator report/applicability/skip sidecars, compiled profile pins bind each
 * file. The service recomputes raw identities and verifies the exact source-
 * derived skip set. project_service derives the whole B row population from
 * those pinned descriptors (no caller supplies rows) and joins it; begin still
 * returns a failure because #509 checks and command plans are not
 * independently authenticated. The blocked production profile lacks these
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
 * held bin/clang and the held installed inventory file whose bytes were
 * decoded; both are CLOEXEC, read-only and at least 3, as producer_begin
 * requires. plan.template and plan.rows point into this object, so it must
 * stay in place until release. Zero-initialize before import. */
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
    int inventory;
    u32 owned;
} BqRetirementReferencePolicy;

/* Uses the compiled native-retirement profile. The blocked profile has no
 * reference-template/-inventory pins, so this fails closed until reviewed
 * pins exist. It supplies no #508 configuration or #509 authority. */
BUSTER_F_DECL BqError bq_retirement_reference_policy_import(int installed,
    BqRetirementPreparation const* preparation, BqRetirementToolchain const* toolchain,
    BqRetirementReferencePolicy* policy);
BUSTER_F_DECL bool bq_retirement_reference_policy_release(BqRetirementReferencePolicy* policy);

/* The two native stage rows (link, then self-host-stage1) that follow the
 * census object rows in the derived population. */
#define BQ_RETIREMENT_DERIVED_STAGE_ROWS 2u
/* The fixed broker CLI whose path the unit's matched-build receipts bind. */
#define BQ_RETIREMENT_STAGE_BROKER "/usr/local/libexec/buster-bench-systemd-broker"

/* #1020 held, profile-pinned census descriptors (read-only, close-on-exec,
 * single-link regular files); each is checked against its own pin. */
typedef enum BqRetirementCensusFile
{
    BQ_RETIREMENT_CENSUS_SUPPORT_DECLARATION,
    BQ_RETIREMENT_CENSUS_SOURCE_APPLICABILITY_LEDGER,
    BQ_RETIREMENT_CENSUS_INPUTS,
    BQ_RETIREMENT_CENSUS_ROWS,
    BQ_RETIREMENT_CENSUS_MANIFEST,
    BQ_RETIREMENT_CENSUS_VALIDATOR_REPORT,
    BQ_RETIREMENT_CENSUS_VALIDATOR_APPLICABILITY,
    BQ_RETIREMENT_CENSUS_VALIDATOR_SKIPS,
    BQ_RETIREMENT_CENSUS_FILE_COUNT
} BqRetirementCensusFile;

typedef struct BqRetirementCensusFiles
{
    int descriptors[BQ_RETIREMENT_CENSUS_FILE_COUNT];
} BqRetirementCensusFiles;

/* #1020 B population derived by the service from the pinned census, never
 * supplied by a caller: census object rows in ordinal order, then the two
 * native stage rows. prepared joins A, the matched builds and the binaries.
 * population_sha256 seals the rows rows_join accepted; authority_begin must
 * receive this exact array. Zero-initialize; release on every path. */
typedef struct BqRetirementProjection
{
    BqRetirementPrepared prepared;
    BqRetirementTrustedRow* rows;
    char population_sha256[SHA256_HEX_CAPACITY];
    char evidence_sha256[SHA256_HEX_CAPACITY];
    u64 job_id, attempt_token;
    u32 owned;
} BqRetirementProjection;

/* Reads A, the matched-build sequence and the binary record from stores
 * (the unit's sealed export and retirement-build/), never the queue, with
 * the compiled profile, the fixed broker binding and a full-census report.
 * native_target is the pinned reference template's. Returns BQ_OK only when
 * bq_retirement_validator_rows_join accepts the derived rows. */
BUSTER_F_DECL BqError bq_retirement_correctness_project_service(BqRetirementBuildStores stores,
    BqJob const* job, int installed, int workspaces, BqRetirementCensusFiles const* census,
    u32 native_target, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementProjection* projection);
BUSTER_F_DECL bool bq_retirement_projection_release(BqRetirementProjection* projection);
/* Still fail-closed: an intact projection returns BQ_RECIPE_MISMATCH (no #509
 * receipts yet), a changed one BQ_SOURCE_MISMATCH; the gate is poisoned. */
BUSTER_F_DECL BqError bq_retirement_correctness_begin_service(BqRetirementProjection const* projection,
    BqRetirementRequiredCheck const* checks, u32 check_count, BqRetirementCheckResult* check_facts,
    BqRetirementRowFact* facts, u32* identity_workspace, u32 identity_slots, u8* census_workspace,
    u32 census_slots, BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate);
#endif
