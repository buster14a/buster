/* Private #1020/#881 lane-B caller inputs inside the service worker-unit.
 * The unit reaches the installed tree read-only and the attempt workspace,
 * never the queue or lease. bq_retirement_unit_prepare re-imports A from the
 * coordinator's export, verifies the pinned toolchain and imports the pinned
 * reference policy. bq_retirement_unit_build then runs both matched trusted
 * builds through the broker launch seam and persists their evidence beside
 * the export. The retirement recipe stays unadmitted; bq_worker_unit calls
 * neither yet.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#include "retirement_correctness_service.h"
#include "phase_channel.h"

/* Unit-written sibling of the coordinator's sealed export: four stage logs,
 * four stage receipts, binaries-<id> and matched-builds-<id>, sealed 0500. */
#define BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY "retirement-build"
#define BQ_RETIREMENT_UNIT_BROKER "/usr/local/libexec/buster-bench-systemd-broker"

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

/* store is the export prepare used. Requires the PREPARING acknowledgement
 * before the first child (sent here unless phases already holds it), then
 * runs baseline and candidate generate/build through the broker. A readable
 * cancellation_fd (the SIGTERM self-pipe) or deadline_ns (CLOCK_MONOTONIC,
 * the lease handoff's execution deadline) kills and reaps the running stage,
 * proves its process group absent and returns BQ_WORKER_CANCEL_SIGNAL or
 * BQ_WORKER_TIMEOUT (BQ_CLEANUP_FAILED if absence is not proven). Uses the
 * compiled profile, so it fails closed with the blocked profile. */
BUSTER_F_DECL BqError bq_retirement_unit_build(BqRetirementStore store, BqRetirementUnitPrepared const* prepared,
    int workspaces, int installed, String8 workspace_root, BqPhaseChannel* phases, int cancellation_fd,
    u64 deadline_ns, BqRetirementUnitBuilt* built);
BUSTER_F_DECL bool bq_retirement_unit_built_release(BqRetirementUnitBuilt* built);

#endif
