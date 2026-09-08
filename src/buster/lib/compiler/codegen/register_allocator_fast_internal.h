#pragma once

#include <buster/lib/compiler/codegen/machine.h>

// Private test seam for the active-lane owner query in register_allocator_fast.c.
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL u64 machine_fast_owner_match_mask_test(u32 const* owner, u64 active, u32 value);
#endif
