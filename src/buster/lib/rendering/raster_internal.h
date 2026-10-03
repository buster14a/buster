#pragma once
#include <buster/lib/rendering_raster.h>
#if BUSTER_LINUX && !defined(BUSTER_RASTER_CPU_ONLY)
#include <xcb/xcb.h>
#endif
#if BUSTER_INCLUDE_TESTS
// Real server readback, restricted to 256x256 to bound its reply allocation.
// This is a pixel-transfer/lifecycle seam, not evidence of GPU rendering.
BUSTER_F_DECL bool rendering_raster_readback_matches_for_test(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas);
#endif
