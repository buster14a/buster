#pragma once

#include <buster/base.h>
#include <buster/arena.h>

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

BUSTER_F_DECL TTF_FontInitialization truetype_font_initialize(ByteSlice file, u32 font_index);
BUSTER_F_DECL f32 truetype_scale_for_pixel_height(const TTF_FontInformation* information, f32 height);
BUSTER_F_DECL TTF_VerticalMetrics truetype_get_font_vertical_metrics(const TTF_FontInformation* information);
BUSTER_F_DECL TTF_HorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TTF_FontInformation* information, u32 codepoint);
BUSTER_F_DECL s32 truetype_get_codepoint_kern_advance(const TTF_FontInformation* information, u32 codepoint_left, u32 codepoint_right);
BUSTER_F_DECL TTF_Bitmap truetype_get_codepoint_bitmap(Arena* arena, const TTF_FontInformation* information, f32 scale_x, f32 scale_y, u32 codepoint);
