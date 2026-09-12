#pragma once

#include <buster/lib/truetype.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool truetype_bitmap_work_is_valid_for_test(u32 width, u32 height, u32 point_count);
#endif
