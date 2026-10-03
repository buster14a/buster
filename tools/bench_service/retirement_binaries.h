/* Private #1018 frozen-binary handoff. The integrator owns the trusted Clang
 * build stages and their complete toolchain/command provenance. This seam
 * binds their frozen outputs to the verified A preparation before B can use
 * either binary; the integrator must authenticate Clang provenance and hold
 * launched binary identity stable. This seam alone cannot authorize timing.
 */
#ifndef BUSTER_BENCH_RETIREMENT_BINARIES_H
#define BUSTER_BENCH_RETIREMENT_BINARIES_H
#include "retirement_prepare.h"

/* The matched-build helper's private per-attempt directory (#1018, #1020):
 * job-<id>-attempt-<token>/retirement-work, service-owned with no group or
 * other access. It holds the stage logs and trusted-build/. The attempt
 * itself stays the materializer's 02710 so the candidate can traverse it. */
#define BQ_RETIREMENT_BUILD_WORK_DIRECTORY "retirement-work"

typedef struct BqRetirementBinaries
{
    char preparation_sha256[SHA256_HEX_CAPACITY];
    char directory_identity_sha256[SHA256_HEX_CAPACITY];
    char source_sha256[2][SHA256_HEX_CAPACITY];
    char binary_sha256[2][SHA256_HEX_CAPACITY];
    char binary_identity_sha256[2][SHA256_HEX_CAPACITY];
} BqRetirementBinaries;

/* Where a build sequence reads A and writes its evidence (binaries, stage
 * logs and receipts, final build record). The queue API uses the queue for
 * both. The worker unit, which cannot reach the queue, reads A from its sealed
 * export and writes into the attempt's retirement-build directory. Borrowed. */
typedef struct BqRetirementBuildStores
{
    BqRetirementStore preparation;
    BqRetirementStore evidence;
} BqRetirementBuildStores;

typedef struct BqRetirementHeldBinaries
{
    BqRetirementBinaries verified;
    /* Kept open across correctness and through the last timed launch. The
     * launcher must execute these exact descriptors, not reopen a pathname. */
    int descriptors[2];
    u32 owned;
} BqRetirementHeldBinaries;

/* The build producer must first freeze both successful trusted Clang outputs
 * as mode-read-only, single-link files in the service-private trusted-build
 * directory beneath BQ_RETIREMENT_BUILD_WORK_DIRECTORY. Both calls independently import A and read the frozen files. */
BUSTER_F_DECL BqError bq_retirement_binaries_record(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char record_sha256[SHA256_HEX_CAPACITY]);
BUSTER_F_DECL BqError bq_retirement_binaries_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementBinaries* verified);
/* The holder must be zero-initialized or previously released. Acquisition
 * refuses to overwrite live descriptors; release accepts a zeroed holder. */
BUSTER_F_DECL BqError bq_retirement_binaries_acquire(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementHeldBinaries* held);
BUSTER_F_DECL void bq_retirement_binaries_release(BqRetirementHeldBinaries* held);

#endif
