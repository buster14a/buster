/* Private #1018 matched-build handoff. The stage launcher binds the exact
 * fixed plan to a child wait and service-owned log. The worker still owns
 * isolation, deadlines and cancellation through the whole attempt.
 *
 * Launch seam (#1020): a DIRECT sequence forks the verified driver itself
 * (fixture and legacy queue API). A BROKER sequence forks only the fixed
 * systemd broker CLI with a typed start-stage request, so each subject runs
 * in its own broker stage unit as its stage user. The broker constructs the
 * same argv, cwd, environment and umask from retirement_stage.h.
 *
 * Layout (#1018, #1020): the attempt stays the materializer's traversable
 * 02710. The helper owns the private retirement-work/ directory (logs and
 * trusted-build/) and creates each subject's configured root fresh before
 * its generate: base/build/matched-build (02700, service only) and
 * candidate/matched-build (02770, candidate group). A broker stage can write
 * only its own root and cannot replace it. */
#ifndef BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H
#define BUSTER_BENCH_RETIREMENT_MATCHED_BUILD_H

#include "retirement_binaries.h"
#include "retirement_toolchain.h"
#include "retirement_stage.h"
#include <sys/types.h>

#define BQ_RETIREMENT_BUILD_STAGES BQ_RETIREMENT_STAGE_COUNT
#define BQ_RETIREMENT_BUILD_PATH_CAP 512u
/* Exact modes of the service-created configured roots. */
#define BQ_RETIREMENT_BUILD_BASE_ROOT_MODE 02700
#define BQ_RETIREMENT_BUILD_CANDIDATE_ROOT_MODE 02770

typedef struct BqRetirementBuildStage
{
    /* Exactly this argv, cwd and environment must be handed to the trusted
     * stage runner. Nothing in a public request selects any of these bytes. */
    char const* argv[20];
    char const* env[5];
    char const* cwd;
    /* BROKER sequences: the fixed broker and typed stage name bound into the
     * command digest; the broker, not this process, executes the stage. */
    char const* broker;
    char const* broker_stage;
    u32 argc;
    mode_t file_umask;
} BqRetirementBuildStage;

enum
{
    BQ_RETIREMENT_LAUNCH_DIRECT = 0,
    BQ_RETIREMENT_LAUNCH_BROKER = 1
};

typedef struct BqRetirementMatchedBuild
{
    char workspace[BQ_RETIREMENT_BUILD_PATH_CAP];
    char attempt[BQ_RETIREMENT_BUILD_PATH_CAP];
    /* The private retirement-work directory: logs and trusted-build/. */
    char work[BQ_RETIREMENT_BUILD_PATH_CAP];
    /* Configured root per subject (0 baseline, 1 candidate). */
    char build[2][BQ_RETIREMENT_BUILD_PATH_CAP];
    char source[2][BQ_RETIREMENT_BUILD_PATH_CAP];
    /* Imported A facts rechecked through the cwd descriptor at every launch. */
    BqRetirementSource prepared_source[2];
    char driver[BQ_RETIREMENT_BUILD_PATH_CAP];
    char broker[BQ_RETIREMENT_BUILD_PATH_CAP];
    BqRetirementToolchain toolchain;
    char preparation_sha256[SHA256_HEX_CAPACITY];
    char driver_sha256[SHA256_HEX_CAPACITY];
    char command_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char log_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char stage_receipt_sha256[BQ_RETIREMENT_BUILD_STAGES][SHA256_HEX_CAPACITY];
    char binary_record_sha256[SHA256_HEX_CAPACITY];
    char build_record_sha256[SHA256_HEX_CAPACITY];
    /* Held between a successful generate and its matching build launch. */
    int generated_root;
    u64 generated_device, generated_inode;
    u64 job_id, attempt_token;
    u32 next;
    u32 launcher;
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
    int directory, writer, reader, build_root;
    /* The child leads its own process group so cancellation reaches, and
     * proves absent, every descendant that did not leave it. */
    pid_t process, group;
    u64 directory_device, directory_inode, log_device, log_inode;
    /* Every stage holds its configured root: a generate the root it created
     * just before launch, a build the root its generate completed. */
    u64 build_device, build_inode;
    u64 log_bytes;
    char name[32], command_sha256[SHA256_HEX_CAPACITY];
    u32 stage, state, launcher;
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
/* Release a generated root if the job ends before its matching build. Do not
 * call while a stage child is running; the worker first cancels and reaps it. */
BUSTER_F_DECL bool bq_retirement_matched_build_release(BqRetirementMatchedBuild* build);
/* A fresh log is created exclusively in the held work directory. Poll
 * drains bounded child output, returning 0 while running or draining, 1 for
 * exit zero with complete capture, and -1 for a child/capture/wait failure.
 * A reaped nonzero child can still supply durable failure evidence. Abort
 * retains a running child for the worker to cancel and reap. */
BUSTER_F_DECL bool bq_retirement_matched_build_launch(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess* process);
BUSTER_F_DECL int bq_retirement_matched_build_poll(BqRetirementBuildProcess* process);
BUSTER_F_DECL void bq_retirement_matched_build_abort(BqRetirementBuildProcess* process);
/* SIGKILL the stage's process group, reap the child and prove the group
 * absent before deadline_ns (CLOCK_MONOTONIC). A BROKER stage also asks the
 * broker to KILL its stage unit. The process is then settled for abort. */
BUSTER_F_DECL bool bq_retirement_matched_build_cancel(BqRetirementMatchedBuild const* build,
    BqRetirementBuildProcess* process, u64 deadline_ns);
BUSTER_F_DECL BqError bq_retirement_matched_build_complete(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, BqRetirementBuildProcess* process,
    BqRetirementMatchedBuild* build);
BUSTER_F_DECL BqError bq_retirement_matched_build_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified);

#endif
