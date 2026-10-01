#pragma once
#include <buster/lib/base.h>
#include <buster/lib/window.h>

// Allocation-free CPU raster drawing and explicit Linux/XCB presentation.
// Caller owns all pixel storage; source storage remains live through draw and
// canvas storage remains live through present. Neither operation retains it.
// EXIF orientation 1..8 changes sampling, never the decoded image allocation.
// Color samples are interpreted as supplied; no ICC/gamma conversion occurs.
#define BUSTER_RASTER_MAX_DIMENSION ((u32)4096)
#define BUSTER_RASTER_MAX_CANVAS_BYTES ((u64)64 * 1024 * 1024)
#define BUSTER_RASTER_MIN_ZOOM (1.0 / 65536.0)
#define BUSTER_RASTER_MAX_ZOOM (65536.0)
#define BUSTER_RASTER_CHECKER_SIZE ((u32)16)
#define BUSTER_RASTER_CHECKER_DARK ((u8)48)
#define BUSTER_RASTER_CHECKER_LIGHT ((u8)72)

typedef struct RenderingRasterCanvas RenderingRasterCanvas;
struct RenderingRasterCanvas
{
    ByteSlice pixels;
    u32 width;
    u32 height;
    u32 stride;
    u32 reserved;
};

typedef struct RenderingRasterSource RenderingRasterSource;
struct RenderingRasterSource
{
    ByteSlice pixels;
    u32 width;
    u32 height;
    u32 stride;
    u32 orientation;
};

typedef struct RenderingRasterView RenderingRasterView;
struct RenderingRasterView
{
    // Display-oriented image's top-left position, measured in canvas pixels.
    f64 x;
    f64 y;
    f64 zoom;
};

typedef struct RenderingRasterPresenter RenderingRasterPresenter;
struct RenderingRasterPresenter
{
    WmNativeSurface surface;
    u32 gc;
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
    u8 depth;
    u8 bits_per_pixel;
    u8 scanline_pad;
    u8 byte_order;
    bool initialized;
    u8 reserved[3];
};

// A canvas must be nonempty RGBA8, at most 4096 per axis and 64 MiB including
// row padding. A source is canonical straight-alpha RGBA8; {0} is an empty
// source. Source/canvas storage must not overlap. Invalid admission leaves the
// canvas unchanged. Valid drawing fills every active canvas pixel with opaque
// RGBA8, compositing the nearest source sample over a 16-pixel checkerboard.
BUSTER_F_DECL bool rendering_raster_canvas_is_valid(RenderingRasterCanvas canvas);
BUSTER_F_DECL bool rendering_raster_draw(RenderingRasterCanvas canvas, RenderingRasterSource source, RenderingRasterView view);
// Zero-initialize the presenter. Native initialization allocates only one XCB
// graphics context. Unsupported surfaces/visuals and native errors return false.
// Only Linux XCB TrueColor visuals with 16/24/32-bit pixels are admitted.
BUSTER_F_DECL bool rendering_raster_initialize(RenderingRasterPresenter* presenter, WmNativeSurface surface);
// Presentation copies canvas samples into bounded native request chunks.
// A failed call may have presented a prefix; the next successful call redraws
// the entire canvas. The surface must remain live through deinitialization.
BUSTER_F_DECL bool rendering_raster_present(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas);
// Releases the graphics context; safe to call twice on a zeroed/ended handle.
BUSTER_F_DECL bool rendering_raster_deinitialize(RenderingRasterPresenter* presenter);
