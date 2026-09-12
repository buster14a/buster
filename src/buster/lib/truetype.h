#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>

typedef enum TTF_FontInitializationResult
{
    TTF_FONT_INITIALIZATION_SUCCESS,
    TTF_FONT_INITIALIZATION_FAILED,
    TTF_FONT_INITIALIZATION_UNSUPPORTED,
    TTF_FONT_INITIALIZATION_COUNT,
} TTF_FontInitializationResult;

typedef struct TTF_FontInformation TTF_FontInformation;
struct TTF_FontInformation
{
    ByteSlice data;
    u64 font_start;
    u64 cmap;
    u64 cmap_subtable;
    u64 loca;
    u64 head;
    u64 glyf;
    u64 hhea;
    u64 hmtx;
    u64 kern;
    u32 cmap_format;
    u16 units_per_em;
    u16 num_glyphs;
    u16 num_hmetrics;
    s32 index_to_loc_format;
    u16 max_points;
    u16 max_contours;
    u16 max_composite_points;
    u16 max_composite_contours;
};

typedef struct TTF_FontInitialization TTF_FontInitialization;
struct TTF_FontInitialization
{
    TTF_FontInitializationResult result;
    TTF_FontInformation information;
};

typedef struct TTF_VerticalMetrics TTF_VerticalMetrics;
struct TTF_VerticalMetrics
{
    s32 ascent;
    s32 descent;
    s32 line_gap;
};

typedef struct TTF_HorizontalMetrics TTF_HorizontalMetrics;
struct TTF_HorizontalMetrics
{
    s32 advance_width;
    s32 left_side_bearing;
};

typedef struct TTF_Bitmap TTF_Bitmap;
struct TTF_Bitmap
{
    u8* pixels;
    s32 width;
    s32 height;
    s32 x_offset;
    s32 y_offset;
};

// Per-glyph policy for the retained UI rasterizer. Each pixel is one byte;
// the pixel budget also bounds the 4 by 4 coverage grid to 16,777,216 samples.
#define BUSTER_TTF_MAX_SCALE 65536.0f
#define BUSTER_TTF_MAX_BITMAP_WIDTH 4096u
#define BUSTER_TTF_MAX_BITMAP_HEIGHT 4096u
#define BUSTER_TTF_MAX_BITMAP_PIXELS 1048576u
#define BUSTER_TTF_MAX_RASTER_POINTS 1048576u
#define BUSTER_TTF_MAX_RASTER_EDGE_STEPS 67108864u

BUSTER_F_DECL TTF_FontInitialization truetype_font_initialize(ByteSlice file, u32 font_index);
BUSTER_F_DECL f32 truetype_scale_for_pixel_height(const TTF_FontInformation* information, f32 height);
BUSTER_F_DECL TTF_VerticalMetrics truetype_get_font_vertical_metrics(const TTF_FontInformation* information);
BUSTER_F_DECL TTF_HorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TTF_FontInformation* information, u32 codepoint);
BUSTER_F_DECL s32 truetype_get_codepoint_kern_advance(const TTF_FontInformation* information, u32 codepoint_left, u32 codepoint_right);
// Scales must be finite and in [0, BUSTER_TTF_MAX_SCALE]. A zero scale on
// either axis, an empty glyph, invalid bounds/scales, or an exceeded bitmap
// or raster-work budget returns an all-zero bitmap. Bounds and dimensions
// are checked before allocating outline or pixel storage. Adaptive curves use
// a 0.25px device-space tolerance and bounded subdivision; count-then-emit
// extraction enforces the point and edge-search work budgets before allocating
// the exact raster path, and checks the edge budget again before rasterization.
BUSTER_F_DECL TTF_Bitmap truetype_get_codepoint_bitmap(Arena* arena, const TTF_FontInformation* information, f32 scale_x, f32 scale_y, u32 codepoint);

#if BUSTER_INCLUDE_TESTS
typedef struct TTF_RasterTestPoint TTF_RasterTestPoint;
struct TTF_RasterTestPoint
{
    f32 x;
    f32 y;
};

BUSTER_F_DECL bool truetype_rasterizers_match_for_test(Arena* arena, const TTF_RasterTestPoint* points, u32 point_count, const u32* contour_ends,
                                                       u32 contour_count, u32 width, u32 height);
#endif
