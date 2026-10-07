#pragma once

#include <buster/lib/compiler/gpu/gpu.h>

typedef enum GpuAliasStatus
{
    GPU_ALIAS_NONE,
    GPU_ALIAS_INPUT,
    GPU_ALIAS_QUERY_FAILED,
} GpuAliasStatus;

// Whether `output` names the same file as any of `inputs` (see gpu.c).
BUSTER_F_DECL GpuAliasStatus gpu_output_alias_status(String8 output, String8* inputs, u32 input_count);

#if BUSTER_INCLUDE_TESTS
// Affect only this thread's next owned-workspace deletion; production builds
// contain neither the seam nor its state. Saved temporaries do not consume it.
BUSTER_F_DECL void gpu_test_fail_next_cleanup(bool enabled);
#endif

#if BUSTER_INCLUDE_TESTS
// Returns and clears this thread's count of bytes copied while building result
// logs (per-chunk admission plus the single final flatten), so a test can show
// the work is linear in the retained output.
BUSTER_F_DECL u64 gpu_test_take_log_copied_bytes(void);
#endif
