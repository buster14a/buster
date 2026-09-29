/* Private #1018 -> #1020 staged projection check. In addition to pinned #508
 * support, source applicability ledger, inputs, rows, manifest, and schema-2
 * validator report/applicability/skip sidecars, compiled profile pins bind each
 * file. The service recomputes raw identities and verifies the exact source-
 * derived skip set. project_service derives the whole B row population from
 * those pinned descriptors (no caller supplies rows) and joins it; begin still
 * returns a failure because #509 checks and command plans are not
 * independently authenticated. The blocked production profile lacks these
 * pins and fails closed earlier. The reference-policy importer below has the
 * same property for its reference-template/-inventory pins, and the #509
 * required-check importer (bq_retirement_required_checks_import) for its
 * required-checks pin.
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

/* #508's minimum stage population beyond the census object rows: at least
 * one link and one self-host-stage1 row (min_stage_rows in the binding). */
#define BQ_RETIREMENT_MIN_STAGE_ROWS 2u
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
    /* #508's canonical performance-row population (ROW_SCHEMA v2). */
    BQ_RETIREMENT_CENSUS_PERFORMANCE_ROWS,
    BQ_RETIREMENT_CENSUS_FILE_COUNT
} BqRetirementCensusFile;

typedef struct BqRetirementCensusFiles
{
    int descriptors[BQ_RETIREMENT_CENSUS_FILE_COUNT];
} BqRetirementCensusFiles;

/* #1020 B population imported by the service from #508's pinned
 * performance-row artifact and joined to the pinned census, never supplied by
 * a caller: census object rows in ordinal order, then the declared link and
 * self-host-stage1 rows. prepared joins A, the matched builds and the
 * binaries.
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
 * native_target is the pinned reference template's; it decides native-runtime
 * applicability as the binding's _native_runtime_required does. A profile
 * without performance-rows-sha256 fails closed. Returns BQ_OK only when
 * bq_retirement_validator_rows_join accepts the imported rows. */
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

/* #509 required-check authority (#1020 design step 9). The complete list of
 * #509 checks a retirement attempt must pass comes from one installed file,
 * recipes/BQ_RETIREMENT_REQUIRED_CHECKS_NAME, whose SHA-256 must equal the
 * compiled profile's required-checks-sha256= pin; the candidate tree, the
 * request and the result never name a check. Its executables are installed
 * tools under recipes/BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY/, each pinned by
 * digest inside the authority, and the two held matched binaries. The
 * blocked profile carries no pin, so the import fails closed. */
#define BQ_RETIREMENT_REQUIRED_CHECKS_NAME "native-retirement-performance-v1.required-checks"
#define BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY "native-retirement-performance-v1.checks"
#define BQ_RETIREMENT_REQUIRED_CHECKS_BYTES_CAP (4u * 1024u * 1024u)
#define BQ_RETIREMENT_CHECK_TOOLS_CAP 16u
#define BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_CHECK_ARGUMENTS_CAP 64u
#define BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP 32u
/* One argument, environment entry or configuration line, in bytes. */
#define BQ_RETIREMENT_CHECK_FIELD_CAP 4096u
/* Each check's own wall bound; the job deadline still bounds every child. */
#define BQ_RETIREMENT_CHECK_TIMEOUT_MAX_SECONDS 3500u
#define BQ_RETIREMENT_CHECK_MEMORY_MIN_MIB 16u
#define BQ_RETIREMENT_CHECK_MEMORY_MAX_MIB (1024u * 1024u)
#define BQ_RETIREMENT_CHECK_RECEIPT_CAP 2048u

/* How honestly a check's evidence is labelled (#509): only a check on the
 * unit's native target (or a host-wide check, target 0) may claim native
 * execution; emulated, compile-only and link-only never substitute for it. */
typedef enum BqRetirementCheckEvidence
{
    BQ_RETIREMENT_CHECK_EVIDENCE_NATIVE = 1,
    BQ_RETIREMENT_CHECK_EVIDENCE_EMULATED,
    BQ_RETIREMENT_CHECK_EVIDENCE_COMPILE_ONLY,
    BQ_RETIREMENT_CHECK_EVIDENCE_LINK_ONLY,
    BQ_RETIREMENT_CHECK_EVIDENCE_COUNT
} BqRetirementCheckEvidence;

/* One decoded check. Arguments, environment and configuration point into
 * the authority's decoded text. An argument or environment value may hold
 * one {{binary:0|1}}, {{source:0|1}}, {{tool:N}} or {{work}} token, which
 * the runner resolves to a held descriptor's /proc/self/fd path; argv[0] is
 * exactly a binary or tool token. output_sha256 is the pinned digest of the
 * passing check's complete stdout. */
typedef struct BqRetirementCheckPlan
{
    char const* configuration;
    char const* arguments[BQ_RETIREMENT_CHECK_ARGUMENTS_CAP];
    char const* environment[BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP];
    u32 kind, target, rows, evidence, timeout_seconds, memory_mib;
    u32 argument_count, environment_count;
    char output_sha256[SHA256_HEX_CAPACITY];
} BqRetirementCheckPlan;

/* The imported authority for one attempt. checks[i] is the gate's required
 * check: kind, target, rows, the descriptor-free command digest, the
 * configuration digest and the receipt digest a passing run of this exact
 * attempt (job, token, request, A, sources and both binaries) must produce.
 * tools are held read-only, close-on-exec descriptors (at least 3) of the
 * pinned installed tools. Zero-initialize; release on every path. */
typedef struct BqRetirementRequiredChecks
{
    BqRetirementRequiredCheck* checks;
    BqRetirementCheckPlan* plans;
    char* text;
    int tools[BQ_RETIREMENT_CHECK_TOOLS_CAP];
    char tool_sha256[BQ_RETIREMENT_CHECK_TOOLS_CAP][SHA256_HEX_CAPACITY];
    char authority_sha256[SHA256_HEX_CAPACITY], attempt_sha256[SHA256_HEX_CAPACITY];
    char request_sha256[SHA256_HEX_CAPACITY], preparation_sha256[SHA256_HEX_CAPACITY];
    char population_sha256[SHA256_HEX_CAPACITY];
    char source_sha256[2][SHA256_HEX_CAPACITY], binary_sha256[2][SHA256_HEX_CAPACITY];
    u64 job_id, attempt_token;
    u32 count, tool_count, native_target, owned;
} BqRetirementRequiredChecks;

/* The facts one check receipt records: the sources and binaries the check
 * ran against, how its child ended and its stdout digest. signal is the
 * terminating signal (0 for a normal exit, when exit_code is its status). */
typedef struct BqRetirementCheckOutcome
{
    char source_sha256[2][SHA256_HEX_CAPACITY], binary_sha256[2][SHA256_HEX_CAPACITY];
    char output_sha256[SHA256_HEX_CAPACITY];
    int exit_code, signal;
    u32 timed_out, out_of_memory, failures;
} BqRetirementCheckOutcome;

/* Imports the authority with the compiled profile for job's attempt over
 * projection (the sealed B population, whose support, census, population,
 * native target, row counts, A digest, sources and binaries the authority
 * must name). It requires every check kind, a semantic check for each of the
 * six #509 native hosts, no duplicate (kind, target, configuration), census
 * rows equal to the object rows, matrix and no-fallback rows equal to the
 * compiler-eligible rows, and honest evidence labels. The blocked profile
 * has no pin: BQ_RECIPE_MISMATCH before any file is read. */
BUSTER_F_DECL BqError bq_retirement_required_checks_import(int installed, BqJob const* job,
    BqRetirementProjection const* projection, BqRetirementRequiredChecks* checks);
BUSTER_F_DECL bool bq_retirement_required_checks_release(BqRetirementRequiredChecks* checks);
/* The canonical BQ-RETIREMENT-CHECK-RECEIPT-V1 bytes of check index with
 * outcome. The expected receipt uses the authority's sources and binaries,
 * exit 0, no signal, timeout, OOM or failure and the pinned output. */
BUSTER_F_DECL bool bq_retirement_check_receipt_format(BqRetirementRequiredChecks const* checks, u32 index,
    BqRetirementCheckOutcome const* outcome, char* text, u32 capacity, u32* length);
#endif
