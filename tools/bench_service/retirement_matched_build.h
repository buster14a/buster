/* Private #1018 matched-build handoff. The stage launcher binds the exact
 * fixed plan to a child wait and service-owned log. The worker still owns
 * isolation, deadlines and cancellation through the whole attempt. */
#ifndef BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H
#define BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H

#include "retirement_binaries.h"
#include "retirement_toolchain.h"
#include <sys/types.h>

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
    mode_t file_umask;
} BqRetirementBuildStage;

typedef struct BqRetirementMatchedBuild
{
    char workspace[BQ_RETIREMENT_BUILD_PATH_CAP];
    char attempt[BQ_RETIREMENT_BUILD_PATH_CAP];
    char build[BQ_RETIREMENT_BUILD_PATH_CAP];
    char source[2][BQ_RETIREMENT_BUILD_PATH_CAP];
    /* Imported A facts rechecked through the cwd descriptor at every launch. */
    BqRetirementSource prepared_source[2];
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

enum
{
    BQ_RETIREMENT_BUILD_RUNNING = 1,
    BQ_RETIREMENT_BUILD_REAPED = 2,
    BQ_RETIREMENT_BUILD_WAIT_FAILED = 3,
    BQ_RETIREMENT_BUILD_DRAINING = 4
};

typedef struct BqRetirementBuildProcess
{
    int directory, writer, reader;
    pid_t process;
    u64 directory_device, directory_inode, log_device, log_inode;
    /* Build stages retain the configured root observed before child launch. */
    u64 build_device, build_inode;
    u64 log_bytes;
    char name[32], command_sha256[SHA256_HEX_CAPACITY];
    u32 stage, state;
    int exit_code;
    bool log_overflow, capture_failed, log_eof;
} BqRetirementBuildProcess;

/* Production must supply the compiled fixed driver and sandboxed trusted
 * stage runner; the pinned variant permits an isolated fixture profile. */
BUSTER_F_DECL BqError bq_retirement_matched_build_begin(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* build);
BUSTER_F_DECL bool bq_retirement_matched_build_stage(BqRetirementMatchedBuild* build,
    BqRetirementBuildStage* stage);
/* A fresh log is created exclusively in the held attempt directory. Poll
 * drains bounded child output, returning 0 while running or draining, 1 for
 * exit zero with complete capture, and -1 for a child/capture/wait failure.
 * A reaped nonzero child can still supply durable failure evidence. Abort
 * retains a running child for the worker to cancel and reap. */
BUSTER_F_DECL bool bq_retirement_matched_build_launch(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess* process);
BUSTER_F_DECL int bq_retirement_matched_build_poll(BqRetirementBuildProcess* process);
BUSTER_F_DECL void bq_retirement_matched_build_abort(BqRetirementBuildProcess* process);
BUSTER_F_DECL BqError bq_retirement_matched_build_complete(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, BqRetirementBuildProcess* process,
    BqRetirementMatchedBuild* build);
BUSTER_F_DECL BqError bq_retirement_matched_build_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified);

#endif
