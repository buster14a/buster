#pragma once

#include <buster/lib/compiler/gpu/gpu.h>

#if BUSTER_INCLUDE_TESTS
// Affect only this thread's next owned-workspace deletion; production builds
// contain neither the seam nor its state. Saved temporaries do not consume it.
BUSTER_F_DECL void gpu_test_fail_next_cleanup(bool enabled);
#endif
