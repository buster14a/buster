/* Private #1018 -> #1020 staged projection check. In addition to pinned #508
 * support, source applicability ledger, inputs, rows, manifest, and schema-2
 * validator report/applicability/skip sidecars, compiled profile pins bind each
 * file. The service recomputes raw identities and verifies the exact source-
 * derived skip set. It deliberately returns a failure before begin because
 * per-row configuration, #509 checks, oracle evidence, and command plans are
 * not independently authenticated. The blocked production profile lacks these
 * pins and fails closed earlier.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_SERVICE_H
#include "retirement_binaries.h"
#include "retirement_matched_build.h"
#include "retirement_correctness.h"

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
