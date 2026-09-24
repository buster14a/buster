/* Private #1020 readback of service-frozen compiler outputs. The target ID
 * follows the twelve-entry TARGETS order in the frozen performance contract.
 * The service supplies exact output names from its frozen command plan.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ARTIFACT_SERVICE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ARTIFACT_SERVICE_H
#include "retirement_correctness.h"

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
    unsigned armed;
} BqRetirementArtifactStart;

typedef struct BqRetirementRuntimeStart
{
    BqRetirementArtifactStart location;
    uint64_t file_device, file_inode;
    int writer;
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
 * other-user write access. Runtime start creates a fresh empty log; pass its
 * writer to the trusted runner for stdout/stderr, then call
 * finish after reaping the child. The caller owns and closes the returned
 * read descriptor. Abort an unfinished capture with runtime_abort; the
 * partial file remains available as failure evidence. */
BUSTER_F_DECL bool bq_retirement_artifact_start(BqRetirementArtifactLocation location,
    BqRetirementArtifactStart* start);
BUSTER_F_DECL bool bq_retirement_runtime_start(BqRetirementArtifactLocation location,
    BqRetirementRuntimeStart* start);
BUSTER_F_DECL bool bq_retirement_runtime_finish(BqRetirementRuntimeStart* start, int* read_descriptor);
BUSTER_F_DECL void bq_retirement_runtime_abort(BqRetirementRuntimeStart* start);

/* Artifact/code, runtime-output and command digests must be empty on entry.
 * The service runner must bind these exact argv/cwd/environment plans to its
 * completed processes. An inapplicable process has a zeroed command struct.
 * Pass the prelaunch artifact start for each compiler output and the finished
 * runtime start with its read-only CLOEXEC descriptor for each applicable
 * execution. Use -1 and a zeroed start for inapplicable runtimes. The service
 * must bind each actual process and its wait status to these starts, including
 * stdout and stderr in the frozen oracle order. The readback consumes the
 * starts, derives code eligibility and poisons the gate on failed or changed
 * reads. Untimed controls require absent locations and zeroed starts.
 */
BUSTER_F_DECL bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation artifacts[2], BqRetirementArtifactStart starts[2],
    int runtime_outputs[2], BqRetirementRuntimeStart runtime_starts[2],
    BqRetirementRowCommands const commands[2], BqRetirementRowFact const* observed);
#endif
