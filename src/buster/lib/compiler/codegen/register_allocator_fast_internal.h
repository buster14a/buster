#pragma once

#include <buster/lib/compiler/codegen/machine.h>

// Private test seams for register_allocator_fast.c.
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool machine_fast_owner_contains_test(u32 const* owner, u64 active, u32 value);
BUSTER_F_DECL u64 machine_fast_owner_match_mask_test(u32 const* owner, u64 active, u32 value);
BUSTER_F_DECL bool machine_fast_close_live_ranges_test(Arena* arena);
BUSTER_F_DECL bool machine_fast_close_slot_ranges_test(Arena* arena);
#endif
