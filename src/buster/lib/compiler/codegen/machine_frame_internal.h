#pragma once

#include <buster/lib/compiler/codegen/machine.h>

// Shared FAST/QUALITY frame arithmetic. Running offsets stay wide
// until reserve has checked their u32 representation; finish additionally
// enforces the selected encoder's frame and displacement limits.
BUSTER_F_DECL bool machine_stack_frame_reserve(u64* running, u64 size, u32 alignment);
BUSTER_F_DECL bool machine_stack_frame_finish(MachineFunction const* function, u64 running, u32 push_area, u32 push_count,
                                            u32* frame_size);
