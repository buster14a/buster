#pragma once

// Fixture-authoritative ABI labels, not inferred C type information. The host
// trusts the exact accompanying source and compiler command. A symbol name or
// this integer by itself does not establish ABI compatibility for arbitrary C.
#define PILOT_ABI_STATELESS 0x4255535445520001ULL
#define PILOT_ABI_HOST_STATE 0x4255535445520002ULL

typedef struct PilotState PilotState;
struct PilotState
{
    int total;
    unsigned int calls;
};

typedef int PilotFunction(int value);
typedef int PilotStateFunction(PilotState* state, int value);
