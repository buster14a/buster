// Exactly the admission + final best-first heap stage. Inputs are known
// eligible; lifetime/traffic construction, placement pops/probes are excluded.
// This translation unit is compiled separately from the timer, with no LTO.
#include "probe.h"
#define BUSTER_GLOBAL_LOCAL static
#if PROBE_CANDIDATE
#include "candidate.inc"
#else
#include "baseline.inc"
#endif

u32 quality313_select(MachineQualityInterval const* input, u32 population, MachineQualityInterval* heap)
{
    u32 count = 0;
#if PROBE_CANDIDATE
    for (u32 index = 0; index < population; index += 1)
    {
        machine_quality_candidate_offer(heap, &count, PROBE_LIMIT, input[index]);
    }
#else
    // Literal old prefix policy: stop scanning when the fixed budget fills.
    for (u32 index = 0; index < population && count < PROBE_LIMIT; index += 1)
    {
        heap[count] = input[index];
        count += 1;
    }
#endif
    for (u32 root = count / 2; root > 0; root -= 1)
    {
        machine_quality_heap_sift(heap, count, root - 1);
    }
    return count;
}
