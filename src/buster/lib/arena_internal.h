#pragma once
// Calling-thread, one-shot failure control for the arena decommit boundary.
// Production consumers use arena.h; registered arena tests own this seam.
#include <buster/lib/arena.h>

#if BUSTER_INCLUDE_TESTS
// Skip exactly one actual discard attempt. No-page rewinds do not consume it.
BUSTER_F_DECL void arena_test_fail_next_decommit(void);
#endif
