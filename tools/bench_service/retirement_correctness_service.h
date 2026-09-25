/* Private #1018 -> #1020 service join. The caller supplies held descriptors for
 * the pinned #508 support declaration, inputs.tsv, and raw rows.tsv. The input
 * and row files each require separate compiled-profile digests; B declaration
 * digests are consistency checks only. This entry point validates the exact
 * source-ledger join, fixture recipes, complete fixed matrix and #508 canonical
 * row identities, then binds binary identities to A readback. Validator-derived
 * eligibility and per-row configuration receipts still require independent
 * replay and approved authority.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#include "retirement_binaries.h"
#include "retirement_matched_build.h"
#include "retirement_correctness.h"

BUSTER_F_DECL BqError bq_retirement_correctness_begin_service(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, int support_declaration, int census_inputs, int census_rows,
    String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate);
#endif
