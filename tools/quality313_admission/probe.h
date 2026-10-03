#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t u32;
typedef uint64_t u64;
#include "interval.inc"

#define PROBE_LIMIT 4096u
#define PROBE_OPERATIONS 4096u
#define PROBE_MAXIMUM_INPUT 8192u

u32 quality313_select(MachineQualityInterval const* input, u32 population, MachineQualityInterval* heap);
