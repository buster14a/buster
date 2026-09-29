/* Private #1020/#881 lane-B caller inside the service worker-unit.
 * The unit reaches the installed tree read-only and the attempt workspace,
 * never the queue or lease. bq_retirement_unit_prepare re-imports A from the
 * coordinator's export, verifies the pinned toolchain and imports the pinned
 * reference policy. bq_retirement_unit_build then runs both matched trusted
 * builds through the broker launch seam and persists their evidence beside
 * the export. bq_retirement_unit_project imports the B population from
 * #508's pinned performance rows joined to the pinned census, and bq_retirement_unit_oracle runs the oracle
 * authority and the reference producer over that same row array.
 * bq_retirement_unit_gate is the correctness gate, still fail-closed until
 * #509; bq_retirement_unit_ready writes the durable ready record only for an
 * admitted gate, and the coordinator's bq_retirement_unit_replay re-derives
 * every digest that record binds. The retirement recipe stays unadmitted and
 * bq_worker_unit calls none of these yet.
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

/* Installed beside the reference template under recipes/: the nine
 * profile-pinned #508 census, schema-2 validator and performance-row files,
 * by these names, in BqRetirementCensusFile order. */
#define BQ_RETIREMENT_UNIT_CENSUS_DIRECTORY "native-retirement-performance-v1.census"
#define BQ_RETIREMENT_UNIT_CENSUS_FILES "support.tsv", "source-applicability.tsv", "inputs.tsv", "rows.tsv", \
    "manifest.txt", "validator-report.json", "applicability.tsv", "applicability-skips.tsv", \
    "performance-rows.json"
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
 * the queue), and imports the B population from #508's pinned performance
 * rows for the pinned template's native target
 * (bq_retirement_correctness_project_service). The projection must
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
    /* Per reference: the held binary and output directory numbers of its
     * /proc/self/fd runtime command, so the replay can rebuild that command. */
    int* descriptors;
    char attempt_sha256[SHA256_HEX_CAPACITY];
    u32 owned;
} BqRetirementUnitOracle;

/* Design steps 7 and 8: authority_begin over projection->rows (the array
 * rows_join accepted), producer_begin over the pinned plan, held inventory
 * and held Clang with A's materialized source copies as roots and a new
 * private output directory, then producer_next, the producer-built runtime
 * command and authority_next for each reference row, and authority_finish,
 * authority_ready and producer_ready. Every child runs under
 * cancellation_fd (the SIGTERM self-pipe: a close-on-exec, read-only FIFO or
 * socket, else BQ_CONFIGURATION_MISMATCH before any work) and the absolute
 * deadline_ns, each step capped at 3500 s; a
 * readable descriptor returns BQ_WORKER_CANCEL_SIGNAL, an expired deadline
 * BQ_WORKER_TIMEOUT, and any failure releases everything with no facts. The
 * compiled profile must pin prepared's template, so the blocked profile
 * fails closed. */
BUSTER_F_DECL BqError bq_retirement_unit_oracle(BqRetirementUnitPrepared const* prepared,
    BqRetirementProjection* projection, int workspaces, int cancellation_fd, u64 deadline_ns,
    BqRetirementUnitOracle* oracle);
BUSTER_F_DECL bool bq_retirement_unit_oracle_release(BqRetirementUnitOracle* oracle);

/* Design step 9's outcome. Only an issuer the ready record accepts can make
 * one admitted; production has none until #509 same-attempt receipts exist,
 * so a filled-in struct is never an admission. */
typedef struct BqRetirementUnitGate
{
    char seal_sha256[SHA256_HEX_CAPACITY];
    u32 issuer;
} BqRetirementUnitGate;

/* Design step 9: bq_retirement_correctness_begin_service over the sealed
 * projection, after a finished oracle. It fails closed today
 * (BQ_RECIPE_MISMATCH, or BQ_SOURCE_MISMATCH for changed rows) and leaves
 * gate unadmitted; nothing else issues an admitted gate in production. */
BUSTER_F_DECL BqError bq_retirement_unit_gate(BqRetirementProjection const* projection,
    BqRetirementUnitOracle const* oracle, BqRetirementUnitGate* gate);

/* Design step 10's unit-written sibling of retirement-build/: one record,
 * named ready-<its SHA-256>, sealed 0500. */
#define BQ_RETIREMENT_UNIT_READY_DIRECTORY "retirement-ready"

/* Design step 10. Refuses an unadmitted gate with BQ_RECIPE_MISMATCH before
 * examining any object or the attempt, so production writes nothing until
 * #509. Otherwise it rechecks the prepared, built, projected and oracle
 * objects together, seals retirement-work/reference-oracle/ 0500, rehashes
 * its exact closure against the authority and formats the canonical
 * BQ-RETIREMENT-READY-V1 record; only then does it create a new, durable
 * retirement-ready/ and publish the record (O_EXCL temporary, fsync, link
 * to ready-<digest>, unlink, seal 0500, fsync). A second record into one
 * attempt is refused. ready_sha256 receives the digest of a published
 * record, which the coordinator must receive over an authenticated channel
 * for bq_retirement_unit_replay. */
BUSTER_F_DECL BqError bq_retirement_unit_ready(BqRetirementUnitPrepared const* prepared,
    BqRetirementUnitBuilt const* built, BqRetirementProjection const* projection,
    BqRetirementUnitOracle const* oracle, BqRetirementUnitGate const* gate, int workspaces,
    char ready_sha256[SHA256_HEX_CAPACITY]);

/* Coordinator side, outside the unit and read-only. preparation_sha256 is
 * the coordinator's own A digest and ready_sha256 the authenticated record
 * digest. It re-imports A, the toolchain and the reference policy from the
 * export, the matched builds and both binaries from retirement-build/, the
 * census projection, and every reference-oracle/ file, rebuilds each runtime
 * command and the oracle attempt digest, verifies the gate and requires the
 * stored record to equal, byte for byte, the record those facts format. A
 * missing, extra, linked, writable or reordered entry fails closed, as does
 * an unsealed directory or a partial write. Compiled profile: the blocked
 * profile fails closed, and no production gate verifier exists yet. */
BUSTER_F_DECL BqError bq_retirement_unit_replay(BqRetirementStore store, int workspaces, int installed,
    u64 job_id, u64 attempt_token, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY]);

#endif
