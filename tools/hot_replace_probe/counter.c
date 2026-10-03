// Edit PILOT_STEP, save, then enter r in the running hot_reload application.
#include "pilot_contract.h"

#define PILOT_STEP 1ULL

unsigned long long const pilot_descriptor[PILOT_DESCRIPTOR_WORDS] = {PILOT_DESCRIPTOR_VALUES};

unsigned long long pilot_step(PilotState* state)
{
    state->total += pilot_host_delta(PILOT_STEP);
    state->calls += 1;
    return state->total;
}
