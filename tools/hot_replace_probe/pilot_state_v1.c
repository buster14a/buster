#include "pilot_contract.h"

unsigned long long const pilot_abi = PILOT_ABI_HOST_STATE;

int pilot_step(PilotState* state, int value)
{
    state->total += value;
    state->calls += 1;
    return state->total;
}
