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

/* Artifact/code and runtime-output digests must be empty on entry. Runtime
 * outputs are read-only CLOEXEC descriptors of complete service-owned process
 * logs, with stdout and stderr captured in the frozen oracle order. Pass -1
 * for each inapplicable runtime. The service must bind each descriptor to
 * that row's completed process before calling this function. The readback
 * derives code eligibility and poisons the gate on failed or changed reads.
 * For an untimed control, both artifact locations must be absent.
 */
BUSTER_F_DECL bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation locations[2], int runtime_outputs[2],
    BqRetirementRowFact const* observed);
#endif
