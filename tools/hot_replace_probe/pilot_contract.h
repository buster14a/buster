#pragma once

// Trusted application's fixed Linux x86-64 C ABI, not arbitrary C reflection.
// Persistent state belongs to the host and contains no module-owned pointers.
typedef struct PilotState PilotState;
struct PilotState
{
    unsigned long long total;
    unsigned long long calls;
};

typedef unsigned long long PilotStateFunction(PilotState* state);
typedef unsigned long long PilotImportFunction(unsigned long long value);

#define PILOT_ABI_VERSION 1ULL
#define PILOT_STATE_VERSION 1ULL
#define PILOT_DESCRIPTOR_WORDS 7
#define PILOT_DESCRIPTOR_VALUES \
    0x4255535445520002ULL, PILOT_ABI_VERSION, PILOT_STATE_VERSION, \
    sizeof(PilotState), _Alignof(PilotState), \
    __builtin_offsetof(PilotState, total), __builtin_offsetof(PilotState, calls)

// Only this process-lifetime host import is provided. Calls returning through
// it are still active module calls; it cannot retain state or code pointers.
unsigned long long pilot_host_delta(unsigned long long value);
