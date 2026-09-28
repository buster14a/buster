/* Private #1020/#881 lane-B caller inputs inside the service worker-unit.
 * The unit reaches the installed tree read-only and the attempt workspace,
 * never the queue or lease. bq_retirement_unit_prepare re-imports A from the
 * coordinator's export, verifies the pinned toolchain and imports the pinned
 * reference policy. The retirement recipe stays unadmitted; bq_worker_unit
 * does not call this yet.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_H
#include "retirement_correctness_service.h"

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

#endif
