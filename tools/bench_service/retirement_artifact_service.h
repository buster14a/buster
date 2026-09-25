/* Private #1020 readback of service-frozen compiler outputs. The target ID
 * follows the twelve-entry TARGETS order in the frozen performance contract.
 * The service supplies exact output names from its frozen command plan.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ARTIFACT_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ARTIFACT_SERVICE_H
#include "retirement_correctness.h"
#include <sys/types.h>

typedef struct BqRetirementArtifactLocation
{
    int directory;
    char const* name;
} BqRetirementArtifactLocation;

#define BQ_RETIREMENT_OUTPUT_NAME_CAP 128u

typedef struct BqRetirementArtifactStart
{
    int directory;
    char name[BQ_RETIREMENT_OUTPUT_NAME_CAP];
    uint64_t directory_device, directory_inode;
    pid_t process;
    char command_sha256[65];
    unsigned armed, process_state;
} BqRetirementArtifactStart;

enum
{
    BQ_RETIREMENT_ARTIFACT_RUNNING = 1,
    BQ_RETIREMENT_ARTIFACT_REAPED = 2,
    BQ_RETIREMENT_ARTIFACT_FAILED = 3
};

enum
{
    BQ_RETIREMENT_RUNTIME_CREATED = 1,
    BQ_RETIREMENT_RUNTIME_FROZEN = 2,
    BQ_RETIREMENT_RUNTIME_RUNNING = 3,
    BQ_RETIREMENT_RUNTIME_REAPED = 4,
    BQ_RETIREMENT_RUNTIME_FAILED = 5
};

typedef struct BqRetirementRuntimeStart
{
    BqRetirementArtifactStart location;
    uint64_t file_device, file_inode;
    int writer;
    pid_t process;
    char command_sha256[65];
    unsigned state;
} BqRetirementRuntimeStart;

typedef struct BqRetirementProcessCommand
{
    char* const* arguments;
    char* const* environment;
    char const* directory;
    unsigned argument_count, environment_count;
} BqRetirementProcessCommand;

typedef struct BqRetirementRowCommands
{
    BqRetirementProcessCommand compiler, runtime;
} BqRetirementRowCommands;

/* Supply a zeroed start and call before each process. Artifact start records
 * an absent fixed output name under a held service-owned directory without
 * other-user write access. Launch and poll the compiler before readback.
 * Runtime start creates a fresh empty log; pass its
 * writer to the private launcher. Poll through the worker deadline, then
 * freeze with finish after observing exit zero. The caller owns and closes
 * the returned read descriptor. Abort after any live child has been cancelled
 * and reaped; the partial file remains as failure evidence. */
BUSTER_F_DECL bool bq_retirement_artifact_start(BqRetirementArtifactLocation location,
    BqRetirementArtifactStart* start);
/* The runner owns deadlines and cancellation. Poll returns 0 while running,
 * 1 after exit zero, or -1 for a failed wait, signal or nonzero exit. */
BUSTER_F_DECL bool bq_retirement_artifact_launch(BqRetirementArtifactStart* start,
    BqRetirementProcessCommand const* command);
BUSTER_F_DECL int bq_retirement_artifact_poll(BqRetirementArtifactStart* start);
BUSTER_F_DECL void bq_retirement_artifact_abort(BqRetirementArtifactStart* start);
BUSTER_F_DECL bool bq_retirement_runtime_start(BqRetirementArtifactLocation location,
    BqRetirementRuntimeStart* start);
/* Launch the validated command with stdout/stderr on the fresh writer.
 * Poll is nonblocking: 0 means running, 1 means exit zero, -1 fails.
 * The worker owns the deadline, cancellation and process-group cleanup. */
BUSTER_F_DECL bool bq_retirement_runtime_launch(BqRetirementRuntimeStart* start,
    BqRetirementProcessCommand const* command);
BUSTER_F_DECL int bq_retirement_runtime_poll(BqRetirementRuntimeStart* start);
BUSTER_F_DECL bool bq_retirement_runtime_finish(BqRetirementRuntimeStart* start, int* read_descriptor);
BUSTER_F_DECL void bq_retirement_runtime_abort(BqRetirementRuntimeStart* start);

/* Artifact/code, runtime-output and command digests must be empty on entry.
 * The service runner must bind these exact argv/cwd/environment plans to its
 * completed processes. An inapplicable process has a zeroed command struct.
 * Pass the prelaunch artifact start for each compiler output and the finished
 * runtime start with its read-only CLOEXEC descriptor for each applicable
 * execution. Use -1 and a zeroed start for inapplicable runtimes. The service
 * launch and poll bind each compiler and runtime plan to an observed child
 * wait result. The runner owns deadlines and stdout/stderr ordering. Readback
 * consumes the
 * starts, derives code eligibility and poisons the gate on failed or changed
 * reads. Untimed controls require absent locations and zeroed starts.
 */
BUSTER_F_DECL bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation artifacts[2], BqRetirementArtifactStart starts[2],
    int runtime_outputs[2], BqRetirementRuntimeStart runtime_starts[2],
    BqRetirementRowCommands const commands[2], BqRetirementRowFact const* observed);
#endif
