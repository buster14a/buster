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
    u64 vmtx;
    u32 cmap_format;
    u16 units_per_em;
    u16 num_glyphs;
    u16 num_hmetrics;
    s32 index_to_loc_format;
    u16 max_points;
    u16 max_contours;
    u16 max_composite_points;
    u16 max_composite_contours;
    u16 num_vmetrics;
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

// Glyph atlas policy: the atlas is a square of 16 * text_height pixels per
// side, so text_height is limited to keep it at or below 4096 by 4096 pixels.
#define BUSTER_TTF_ATLAS_MAX_TEXT_HEIGHT 256u

typedef enum TTF_AtlasStatus
{
    TTF_ATLAS_SUCCESS,
    TTF_ATLAS_INVALID_TEXT_HEIGHT,
    TTF_ATLAS_INVALID_FONT,
    TTF_ATLAS_GLYPH_DOES_NOT_FIT,
    TTF_ATLAS_COUNT,
} TTF_AtlasStatus;

typedef struct TTF_AtlasBuild TTF_AtlasBuild;
struct TTF_AtlasBuild
{
    FontTextureAtlasDescription description;
    TTF_AtlasStatus status;
    TTF_FontInitializationResult initialization;
};

BUSTER_F_DECL TTF_FontInitialization truetype_font_initialize(ByteSlice file, u32 font_index);
BUSTER_F_DECL f32 truetype_scale_for_pixel_height(const TTF_FontInformation* information, f32 height);
BUSTER_F_DECL TTF_VerticalMetrics truetype_get_font_vertical_metrics(const TTF_FontInformation* information);
BUSTER_F_DECL TTF_HorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TTF_FontInformation* information, u32 codepoint);
// Horizontal advance delta in font units from version-0, format-0 kern
// subtables. Matching values add in table order; override replaces the sum.
// Minimum, cross-stream, vertical and other-format subtables are ignored.
BUSTER_F_DECL s32 truetype_get_codepoint_kern_advance(const TTF_FontInformation* information, u32 codepoint_left, u32 codepoint_right);
// Scales must be finite and in [0, BUSTER_TTF_MAX_SCALE]. A zero scale on
// either axis, an empty glyph, invalid bounds/scales, or an exceeded bitmap
// or raster-work budget returns an all-zero bitmap. Bounds and dimensions
// are checked before allocating outline or pixel storage. Adaptive curves use
// a 0.25px device-space tolerance and bounded subdivision; count-then-emit
// extraction enforces the point and edge-search work budgets before allocating
// the exact raster path, and checks the edge budget again before rasterization.
// Compounds align points with unsigned indices after matrix transformation,
// with eight levels and bounded outline/attachment work. A parent index at or
// past the points accumulated so far, and a child index at or past the child's
// point count, selects the unhinted phantom points pp1..pp4 (left origin, right
// advance, vertical top, vertical bottom) instead; larger indices are invalid.
// Phantoms derive from the glyph's own glyf bounds and hmtx (and vmtx when
// present, otherwise hhea ascent/descent for the vertical pair). A component
// with USE_MY_METRICS gives its phantoms to its composite. Hinting is not
// evaluated, so phantoms are never hint-adjusted. Invalid or out-of-range
// anchors return the same all-zero bitmap and roll back extraction allocations.
BUSTER_F_DECL TTF_Bitmap truetype_get_codepoint_bitmap(Arena* arena, const TTF_FontInformation* information, f32 scale_x, f32 scale_y, u32 codepoint);

// Rasterizes ' '..'~' of the font in memory into a text_height-scaled atlas.
// text_height must be in [1, BUSTER_TTF_ATLAS_MAX_TEXT_HEIGHT]. Glyph sizes come
// from the font file, so every glyph is checked against the atlas bounds in
// every build: a glyph that does not fit (or an unusable font) returns a status
// other than TTF_ATLAS_SUCCESS with an all-zero description and writes nothing
// outside the atlas pixels. Arena allocations made before the failure are not
// released.
BUSTER_F_DECL TTF_AtlasBuild truetype_font_atlas_build(Arena* arena, ByteSlice font_file, u32 text_height);

// Why a candidate font file was or was not selected by
// truetype_font_select_first_usable. NOT_TRIED is zero so a zeroed status
// array reads as "no attempt was made".
typedef enum TTF_FontCandidateStatus
{
    TTF_FONT_CANDIDATE_NOT_TRIED,
    TTF_FONT_CANDIDATE_USABLE,
    TTF_FONT_CANDIDATE_UNREADABLE,
    TTF_FONT_CANDIDATE_MALFORMED,
    TTF_FONT_CANDIDATE_UNSUPPORTED,
    TTF_FONT_CANDIDATE_COUNT,
} TTF_FontCandidateStatus;

// Reads each path in order (file_read, so bundled iOS and APK paths resolve)
// and runs truetype_font_initialize on the first face, exactly the check
// truetype_font_atlas_build applies later. Returns the index of the first
// candidate that initializes, or count when none does. statuses (count
// entries, may be null) receives the outcome of every candidate that was
// tried; entries after the returned index are left untouched. Each file is read
// into scratch memory that is released before the next candidate.
BUSTER_F_DECL u64 truetype_font_select_first_usable(const String8* paths, u64 count, TTF_FontCandidateStatus* statuses);
BUSTER_F_DECL String8 truetype_font_candidate_status_description(TTF_FontCandidateStatus status);

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
