#pragma once
// Calling-thread, one-shot failure control for the arena decommit boundary.
// Production consumers use arena.h; registered arena tests own this seam.
#include <buster/lib/arena.h>

#if BUSTER_INCLUDE_TESTS
// Skip exactly one actual discard attempt. No-page rewinds do not consume it.
BUSTER_F_DECL void arena_test_fail_next_decommit(void);
// Cancel an unconsumed calling-thread reserve fault; true means no reserve
// reached it. Fixture cleanup must not leak that fault to its next consumer.
BUSTER_F_DECL bool arena_test_cancel_reserve_failure(void);
#endif
