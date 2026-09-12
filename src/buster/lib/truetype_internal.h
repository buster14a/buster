#pragma once

#include <buster/lib/truetype.h>

#if BUSTER_INCLUDE_TESTS
typedef struct TTF_QuadraticTestResult TTF_QuadraticTestResult;
struct TTF_QuadraticTestResult
{
    u32 segment_count;
    f32 maximum_error;
    bool subdivision_limit_reached;
    u8 reserved[3];
};

BUSTER_F_DECL TTF_QuadraticTestResult truetype_flatten_quadratic_for_test(TTF_RasterTestPoint from, TTF_RasterTestPoint control, TTF_RasterTestPoint to,
                                                                         f32 scale_x, f32 scale_y);
BUSTER_F_DECL bool truetype_bitmap_work_is_valid_for_test(u32 width, u32 height, u32 point_count);
#endif
