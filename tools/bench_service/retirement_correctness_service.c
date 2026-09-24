/* Service-side A -> B entry for #881.
 * begin_service_pinned is the deterministic test seam for the compiled
 * profile; production uses bq_recipe_profile. Acquire replays the durable A
 * and binary records and holds both exact executable inodes. The caller must
 * separately authenticate Clang provenance and the complete #508/#509 facts.
 */
#include "retirement_correctness_service.h"
#include <string.h>

BUSTER_GLOBAL_LOCAL BqError bq_retirement_correctness_begin_service_pinned(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished;
    BqError result = fresh && held && !held->owned && prepared ? BQ_OK : BQ_RECIPE_MISMATCH;
    bool acquired = false;
    if (result == BQ_OK)
    {
        result = bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, profile,
                                                        preparation_sha256, record_sha256, held);
        acquired = result == BQ_OK;
    }
    if (result == BQ_OK)
    {
        BqRetirementBinaries const* verified = &held->verified;
        char support[SHA256_HEX_CAPACITY] = {0};
        bool same = bq_retirement_profile_sha(profile, S8("support-declaration-sha256="), support) &&
                    !memcmp(prepared->support_sha256, support, SHA256_HEX_CAPACITY) &&
                    !memcmp(prepared->preparation_sha256, verified->preparation_sha256, SHA256_HEX_CAPACITY);
        for (u32 side = 0; same && side < 2; side += 1)
            same = !memcmp(prepared->source_sha256[side], verified->source_sha256[side], SHA256_HEX_CAPACITY) &&
                   !memcmp(prepared->binary_sha256[side], verified->binary_sha256[side], SHA256_HEX_CAPACITY);
        result = same ? BQ_OK : BQ_SOURCE_MISMATCH;
    }
    if (result == BQ_OK && !bq_retirement_correctness_begin(gate, prepared, rows, checks, check_count,
                                                              check_facts, facts, identity_workspace, identity_slots,
                                                              census_workspace, census_slots))
        result = BQ_RECIPE_MISMATCH;
    if (result != BQ_OK)
    {
        if (acquired) bq_retirement_binaries_release(held);
        if (fresh) gate->failed = 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_correctness_begin_service_built_pinned(
    BqQueue* queue, BqJob const* job, int installed, int workspaces, String8 workspace_root,
    String8 profile, char const* fixed_driver,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    bool fresh = gate && !gate->check_count && !gate->failed && !gate->finished;
    BqRetirementMatchedBuild build = {0};
    BqError result = fresh && held && !held->owned ?
        bq_retirement_matched_build_import_pinned(queue, job, installed, workspaces, workspace_root,
            profile, fixed_driver, preparation_sha256, record_sha256, build_record_sha256,
            &build) : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces, profile,
            preparation_sha256, record_sha256, prepared, rows, checks, check_count,
            check_facts, facts, identity_workspace, identity_slots, census_workspace, census_slots,
            held, gate);
    else if (fresh) gate->failed = 1;
    return result;
}

BqError bq_retirement_correctness_begin_service(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementPrepared const* prepared,
    BqRetirementTrustedRow const* rows, BqRetirementRequiredCheck const* checks, u32 check_count,
    BqRetirementCheckResult* check_facts, BqRetirementRowFact* facts,
    u32* identity_workspace, u32 identity_slots, u8* census_workspace, u32 census_slots,
    BqRetirementHeldBinaries* held, BqRetirementCorrectness* gate)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_correctness_begin_service_built_pinned(queue, job,
        installed, workspaces, workspace_root, profile, BQ_RETIREMENT_BUILD_DRIVER,
        preparation_sha256, record_sha256, build_record_sha256, prepared, rows, checks, check_count,
        check_facts, facts, identity_workspace, identity_slots, census_workspace, census_slots,
        held, gate);
    return result;
}
