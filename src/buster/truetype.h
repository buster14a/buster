#pragma once
#include <buster/base.h>

ENUM(FontInformationOffset,
    FONT_INFORMATION_OFFSET_LOCA,
    FONT_INFORMATION_OFFSET_HEAD,
    FONT_INFORMATION_OFFSET_GLYF,
    FONT_INFORMATION_OFFSET_HHEA,
    FONT_INFORMATION_OFFSET_HMTX,
    FONT_INFORMATION_OFFSET_KERN,
    FONT_INFORMATION_OFFSET_GPOS,
    FONT_INFORMATION_OFFSET_COUNT,
);

STRUCT(TtfFontInformation)
{
    ByteSlice file;
    u64 font_offset;
    u64 glyph_count;
    u32 offsets[FONT_INFORMATION_OFFSET_COUNT];
    u32 cmap_index_map;
    u16 index_to_loc_format;
    u8 reserved[6];
};

ENUM_T(TtfFontInitializationResult, u8,
    TTF_FONT_INITIALIZATION_SUCCESS,
    TTF_FONT_INITIALIZATION_CORE_TABLES_NOT_PRESENT,
    TTF_FONT_INITIALIZATION_GLYF_PRESENT_BUT_LOCA_NOT,
    TTF_FONT_INITIALIZATION_GLYF_NOT_PRESENT_AND_CFF_NEITHER,
);

STRUCT(TtfFontInitialization)
{
    TtfFontInformation information;
    TtfFontInitializationResult result;
    u8 reserved[7];
};

STRUCT(TtfVerticalMetrics)
{
    s16 ascent;
    s16 descent;
    s16 line_gap;
    u8 reserved[2];
};

STRUCT(TtfHorizontalMetrics)
{
    s16 advance_width;
    s16 left_side_bearing;
};

STRUCT(TtfBitmap)
{
    u8* pixels;
    s32 width;
    s32 height;
    s32 x_offset;
    s32 y_offset;
};

BUSTER_DECL TtfFontInitialization truetype_font_initialize(ByteSlice file, u64 font_offset);
BUSTER_IMPL f32 truetype_scale_for_pixel_height(const TtfFontInformation* info, f32 pixels);
BUSTER_IMPL TtfVerticalMetrics truetype_get_font_vertical_metrics(const TtfFontInformation* info);
BUSTER_IMPL TtfHorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TtfFontInformation* info, u32 codepoint);
BUSTER_IMPL TtfBitmap truetype_get_codepoint_bitmap(Arena* arena, TtfFontInformation* info, f32 scale_x, f32 scale_y, u32 codepoint);
BUSTER_IMPL s32 truetype_get_glyph_kern_advance(const TtfFontInformation* info, u32 glyph1, u32 glyph2);
BUSTER_IMPL s32 truetype_get_codepoint_kern_advance(TtfFontInformation* info, u32 codepoint1, u32 codepoint2);
