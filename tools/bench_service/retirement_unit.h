/* Private #1020/#881 lane-B caller inside the service worker-unit.
 * The unit reaches the installed tree read-only and the attempt workspace,
 * never the queue or lease. bq_retirement_unit_prepare re-imports A from the
 * coordinator's export, verifies the pinned toolchain and imports the pinned
 * reference policy. bq_retirement_unit_build then runs both matched trusted
 * builds through the broker launch seam and persists their evidence beside
 * the export. bq_retirement_unit_project derives the B population from the
 * pinned census descriptors, and bq_retirement_unit_oracle runs the oracle
 * authority and the reference producer over that same row array. The
 * correctness gate stays fail-closed, the retirement recipe stays unadmitted
 * and bq_worker_unit calls none of these yet.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#include "retirement_correctness_service.h"
#include "phase_channel.h"

/* Unit-written sibling of the coordinator's sealed export: four stage logs,
 * four stage receipts, binaries-<id> and matched-builds-<id>, sealed 0500. */
#define BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY "retirement-build"
#define BQ_RETIREMENT_UNIT_BROKER BQ_RETIREMENT_STAGE_BROKER

/* policy points into itself, so the object must stay in place until release.
 * Zero-initialize before prepare. */
typedef struct BqRetirementUnitPrepared
{
    BqJob job;
    BqRetirementPreparation preparation;
    BqRetirementToolchain toolchain;
    BqRetirementReferencePolicy policy;
    char preparation_sha256[SHA256_HEX_CAPACITY];
    u32 owned;
} BqRetirementUnitPrepared;

/* Opens job-<id>-attempt-<token>/retirement beneath the workspace root without
 * following links. The caller closes store->directory. */
BUSTER_F_DECL BqError bq_retirement_unit_store_open(int workspaces, u64 job_id, u64 attempt_token,
    BqRetirementStore* store);
/* preparation_sha256 is the digest the authenticated lease handoff carried.
 * Uses the compiled native-retirement profile and the fixed toolchain root,
 * so it fails closed with the blocked profile. */
BUSTER_F_DECL BqError bq_retirement_unit_prepare(BqRetirementStore store, int workspaces, int installed,
    u64 job_id, u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitPrepared* prepared);
BUSTER_F_DECL bool bq_retirement_unit_release(BqRetirementUnitPrepared* prepared);

/* Zero-initialize before build; release on every path. */
typedef struct BqRetirementUnitBuilt
{
    /* The re-imported sequence: command, log, receipt and record digests. */
    BqRetirementMatchedBuild verified;
    /* Both frozen executables held open; verified.binary_sha256 names them. */
    BqRetirementHeldBinaries binaries;
    char binary_record_sha256[SHA256_HEX_CAPACITY];
    char build_record_sha256[SHA256_HEX_CAPACITY];
    u32 owned;
} BqRetirementUnitBuilt;

/* store is the export prepare used. workspace_root must be exactly
 * BQ_RETIREMENT_STAGE_WORKSPACE_ROOT, the broker's fixed root, or the build
 * returns BQ_WORKSPACE_MISMATCH before the channel. Requires the PREPARING acknowledgement
 * before the first child (sent here unless phases already holds it), then
 * runs baseline and candidate generate/build through the broker. A readable
 * cancellation_fd (the SIGTERM self-pipe) or deadline_ns (CLOCK_MONOTONIC,
 * the lease handoff's execution deadline) asks the broker to KILL the
 * running stage unit and reaps the broker CLI, whose relayed exit is the
 * absence proof (bq_retirement_matched_build_cancel), then returns
 * BQ_WORKER_CANCEL_SIGNAL or BQ_WORKER_TIMEOUT (BQ_CLEANUP_FAILED if absence
 * is not proven). A stage that settles other than exit 0 with complete
 * capture takes the same KILL and proof before its failure is recorded.
 * Uses the compiled profile, so it fails closed with the blocked profile. */
BUSTER_F_DECL BqError bq_retirement_unit_build(BqRetirementStore store, BqRetirementUnitPrepared const* prepared,
    int workspaces, int installed, String8 workspace_root, BqPhaseChannel* phases, int cancellation_fd,
    u64 deadline_ns, BqRetirementUnitBuilt* built);
BUSTER_F_DECL bool bq_retirement_unit_built_release(BqRetirementUnitBuilt* built);

/* Installed beside the reference template under recipes/: the eight
 * profile-pinned #508 census and schema-2 validator files, by these names. */
#define BQ_RETIREMENT_UNIT_CENSUS_DIRECTORY "native-retirement-performance-v1.census"
#define BQ_RETIREMENT_UNIT_CENSUS_FILES "support.tsv", "source-applicability.tsv", "inputs.tsv", "rows.tsv", \
    "manifest.txt", "validator-report.json", "applicability.tsv", "applicability-skips.tsv"
/* The unit's reference-producer output directory, created new beneath the
 * private retirement-work/ directory of the attempt. */
#define BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY "reference-oracle"

/* Opens the census files through held no-follow descriptors (close-on-exec,
 * at least 3) beneath recipes/BQ_RETIREMENT_UNIT_CENSUS_DIRECTORY. The pins
 * and file checks are the projection's. Close on every path. */
BUSTER_F_DECL BqError bq_retirement_unit_census_open(int installed, BqRetirementCensusFiles* census);
BUSTER_F_DECL bool bq_retirement_unit_census_close(BqRetirementCensusFiles* census);

/* Design step 6: opens the pinned census, re-imports A, the matched builds
 * and the binary record from store and the sealed retirement-build/ (never
 * the queue), and derives the B population for the pinned template's native
 * target (bq_retirement_correctness_project_service). The projection must
 * name built's binaries. Compiled profile: the blocked profile fails closed. */
BUSTER_F_DECL BqError bq_retirement_unit_project(BqRetirementStore store, BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, int workspaces, int installed, BqRetirementProjection* projection);

/* The finished oracle authority for one attempt. authority points into
 * prepared->policy, projection->rows and references, so all three must stay
 * in place until release. Zero-initialize. */
typedef struct BqRetirementUnitOracle
{
    BqRetirementOracleAuthority authority;
    BqRetirementOracleReference* references;
    char attempt_sha256[SHA256_HEX_CAPACITY];
    u32 owned;
} BqRetirementUnitOracle;

/* Design steps 7 and 8: authority_begin over projection->rows (the array
 * rows_join accepted), producer_begin over the pinned plan, held inventory
 * and held Clang with A's materialized source copies as roots and a new
 * private output directory, then producer_next, the producer-built runtime
 * command and authority_next for each reference row, and authority_finish,
 * authority_ready and producer_ready. Every child runs under
 * cancellation_fd (the SIGTERM self-pipe) and the absolute deadline_ns; a
 * readable descriptor returns BQ_WORKER_CANCEL_SIGNAL, an expired deadline
 * BQ_WORKER_TIMEOUT, and any failure releases everything with no facts. The
 * compiled profile must pin prepared's template, so the blocked profile
 * fails closed. */
BUSTER_F_DECL BqError bq_retirement_unit_oracle(BqRetirementUnitPrepared const* prepared,
    BqRetirementProjection* projection, int workspaces, int cancellation_fd, u64 deadline_ns,
    BqRetirementUnitOracle* oracle);
BUSTER_F_DECL bool bq_retirement_unit_oracle_release(BqRetirementUnitOracle* oracle);

#endif
