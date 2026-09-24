/* Private #1018 matched-build handoff. The trusted stage runner owns process
 * isolation; this service-side sequence binds its four completed stages to A
 * and freezes their actual outputs before B can acquire them. */
#ifndef BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H
#define BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H

#include "retirement_binaries.h"
#include "retirement_toolchain.h"

#define BQ_RETIREMENT_BUILD_STAGES 4u
#define BQ_RETIREMENT_BUILD_PATH_CAP 512u

typedef struct BqRetirementBuildStage
{
    /* Exactly this argv, cwd and environment must be handed to the trusted
     * stage runner. Nothing in a public request selects any of these bytes. */
    char const* argv[20];
    char const* env[5];
    char const* cwd;
    u32 argc;
} BqRetirementBuildStage;

typedef struct BqRetirementMatchedBuild
{
    char workspace[BQ_RETIREMENT_BUILD_PATH_CAP];
    char attempt[BQ_RETIREMENT_BUILD_PATH_CAP];
    char build[BQ_RETIREMENT_BUILD_PATH_CAP];
    char source[2][BQ_RETIREMENT_BUILD_PATH_CAP];
    char driver[BQ_RETIREMENT_BUILD_PATH_CAP];
    BqRetirementToolchain toolchain;
    char preparation_sha256[SHA256_HEX_CAPACITY];
    char driver_sha256[SHA256_HEX_CAPACITY];
    char command_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char log_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char stage_receipt_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char binary_record_sha256[SHA256_HEX_CAPACITY];
    char build_record_sha256[SHA256_HEX_CAPACITY];
    u32 next;
    bool failed;
} BqRetirementMatchedBuild;

/* Production must supply the compiled fixed driver and sandboxed trusted
 * stage runner; the pinned variant permits an isolated fixture profile. */
BUSTER_F_DECL BqError bq_retirement_matched_build_begin(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* build);
BUSTER_F_DECL bool bq_retirement_matched_build_stage(BqRetirementMatchedBuild* build,
    BqRetirementBuildStage* stage);
BUSTER_F_DECL BqError bq_retirement_matched_build_complete(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, int stage_log, int stage_exit,
    BqRetirementMatchedBuild* build);
BUSTER_F_DECL BqError bq_retirement_matched_build_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified);

#endif
