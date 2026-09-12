#pragma once

// Private seam for source-map ordering and scratch-lifetime regressions.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void c_test_source_map_sort(Arena* arena, IrSourceRegion* regions, u32 count);
#endif
