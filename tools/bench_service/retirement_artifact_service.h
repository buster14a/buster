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

/* All fields except the artifact/code facts come from the service-owned
 * process and oracle observation. These facts must be empty on entry. The
 * readback supplies actual file and code-section digests, derives code
 * eligibility, and poisons the gate on any missing or changed artifact.
 * For an untimed control, both locations must be absent.
 */
BUSTER_F_DECL bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation locations[2], BqRetirementRowFact const* observed);
#endif
