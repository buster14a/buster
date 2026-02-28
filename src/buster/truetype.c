#pragma once

// Note: this file is heavily based on stb_truetype

#include <buster/truetype.h>
#include <buster/assertion.h>
#include <buster/os.h>
#include <buster/arena.h>

#define bigendian_read_u16_safe(p) ((typeof(*(p)))(__builtin_bswap16(*(u16*)(p))))
#define bigendian_read_s16_safe(p) ((s16)bigendian_read_u16_safe((u16*)(p)))
#define bigendian_read_u32_safe(p) ((typeof(*(p)))(__builtin_bswap32(*(u32*)(p))))
#define bigendian_read_u64_safe(p) ((typeof(*(p)))(__builtin_bswap64(*(u64*)(p))))

ENUM_T(TtfFontVersion, u32, 
    TTF_FONT_VERSION_TRUETYPE = 0x00010000,
    TTF_FONT_VERSION_CFF = 0x4F54544F,
);

STRUCT(Tag)
{
    u8 v[4];
};

STRUCT(Offset32)
{
    u32 v;
};

#define offset32_read(o) ((u32)__builtin_bswap32(*(u32*)(o)))
#define offset32_is_valid(o) ((o) != 0)

STRUCT(TtfTableRecord)
{
    Tag tag;
    u32 checksum;
    u32 offset;
    u32 length;
};

static_assert(sizeof(TtfTableRecord) == 16);

STRUCT(TtfOffsetTable)
{
    TtfFontVersion version;
    u16 table_count;
    u16 search_range;
    u16 entry_selector;
    u16 range_shift;
};

static_assert(sizeof(TtfOffsetTable) == 12);

#define TAG(x) ((Tag) {\
    .v = {\
        [0] = (x)[0],\
        [1] = (x)[1],\
        [2] = (x)[2],\
        [3] = (x)[3],\
    },\
})

BUSTER_GLOBAL_LOCAL Tag font_information_tags[] = {
    [FONT_INFORMATION_OFFSET_LOCA] = TAG("loca"),
    [FONT_INFORMATION_OFFSET_HEAD] = TAG("head"),
    [FONT_INFORMATION_OFFSET_GLYF] = TAG("glyf"),
    [FONT_INFORMATION_OFFSET_HHEA] = TAG("hhea"),
    [FONT_INFORMATION_OFFSET_HMTX] = TAG("hmtx"),
    [FONT_INFORMATION_OFFSET_KERN] = TAG("kern"),
    [FONT_INFORMATION_OFFSET_GPOS] = TAG("GPOS"),
};
static_assert(BUSTER_ARRAY_LENGTH(font_information_tags) == FONT_INFORMATION_OFFSET_COUNT);

BUSTER_GLOBAL_LOCAL TtfOffsetTable ttf_offset_table_read(u8* pointer)
{
    TtfOffsetTable result;
    let offset_table = (TtfOffsetTable*)pointer;
    result.version = bigendian_read_u32_safe(&offset_table->version);
    result.table_count = bigendian_read_u16_safe(&offset_table->table_count);
    result.search_range = bigendian_read_u16_safe(&offset_table->search_range);
    result.entry_selector = bigendian_read_u16_safe(&offset_table->entry_selector);
    result.range_shift = bigendian_read_u16_safe(&offset_table->range_shift);
    return result;
}

BUSTER_GLOBAL_LOCAL bool tag_compare(Tag a, Tag b)
{
    bool result = memcmp(&a, &b, sizeof(a)) == 0;
    return result;
}

BUSTER_GLOBAL_LOCAL TtfTableRecord truetype_table_find(TtfTableRecord* record_pointer, u16 table_count, Tag tag)
{
    TtfTableRecord result = {};

    for (u32 table_i = 0; table_i < table_count; table_i += 1)
    {
        let r = &record_pointer[table_i];
        TtfTableRecord record = {
            .tag = r->tag,
            .checksum = bigendian_read_u32_safe(&r->checksum),
            .offset = offset32_read(&r->offset),
            .length = bigendian_read_u32_safe(&r->length),
        };

        if (tag_compare(tag, record.tag))
        {
            result = record;
            break;
        }
    }

    return result;
}

STRUCT(CffHeader)
{
    u8 major;
    u8 minor;
    u8 header_size;
    u8 offset_size;
};

STRUCT(MaxpTableCommon)
{
    u32 version;
    u16 glyph_count;
    u8 reserved[2];
};

ENUM_T(TtfPlatformId, u16,
    TTF_PLATFORM_ID_UNICODE = 0,
    TTF_PLATFORM_ID_MAC = 1,
    TTF_PLATFORM_ID_ISO = 2,
    TTF_PLATFORM_ID_MICROSOFT = 3,
);

ENUM_T(TtfMsEncodingId, u16,
    TTF_MS_EID_SYMBOL = 0,
    TTF_MS_EID_UNICODE_BMP = 1,
    TTF_MS_EID_SHIFTJIS = 2,
    TTF_MS_EID_UNICODE_FULL = 10,
);

STRUCT(CmapHeader)
{
    u16 version;
    u16 table_count;
};

STRUCT(CmapEncodingRecord)
{
    u16 platform_id;
    u16 encoding_id;
    Offset32 subtable_offset;
};

BUSTER_IMPL TtfFontInitialization truetype_font_initialize(ByteSlice file, u64 font_offset)
{
    TtfFontInitialization init = {};
    BUSTER_CHECK(init.result == TTF_FONT_INITIALIZATION_SUCCESS);

    let font_pointer = file.pointer + font_offset;
    let offset_table = ttf_offset_table_read(font_pointer);
    let records = (TtfTableRecord*)((TtfOffsetTable*)font_pointer + 1);
    let table_count = offset_table.table_count;
    let cmap = truetype_table_find(records, table_count, TAG("cmap"));
    for (FontInformationOffset i = 0; i < FONT_INFORMATION_OFFSET_COUNT; i += 1)
    {
        init.information.offsets[i] = truetype_table_find(records, table_count, font_information_tags[i]).offset;
    }

    if (offset32_is_valid(cmap.offset) && offset32_is_valid(init.information.offsets[FONT_INFORMATION_OFFSET_HEAD]) && offset32_is_valid(init.information.offsets[FONT_INFORMATION_OFFSET_HHEA]) && offset32_is_valid(init.information.offsets[FONT_INFORMATION_OFFSET_HMTX]))
    {
        if (offset32_is_valid(init.information.offsets[FONT_INFORMATION_OFFSET_GLYF]))
        {
            if (!offset32_is_valid(init.information.offsets[FONT_INFORMATION_OFFSET_LOCA]))
            {
                init.result = TTF_FONT_INITIALIZATION_GLYF_PRESENT_BUT_LOCA_NOT;
            }
        }
        else
        {
            let cff2_record = truetype_table_find(records, table_count, TAG("CFF2"));
            let cff_record = truetype_table_find(records, table_count, TAG("CFF "));

            if (offset32_is_valid(cff2_record.offset))
            {
                os_fail();
            }
            else if (offset32_is_valid(cff_record.offset))
            {
                ByteSlice cff_content = { .pointer = file.pointer + cff_record.offset, .length = cff_record.length };
                BUSTER_UNUSED(cff_content);
                os_fail();
                // TODO
            }
            else
            {
                init.result = TTF_FONT_INITIALIZATION_GLYF_NOT_PRESENT_AND_CFF_NEITHER;
            }
        }

        let maxp_record = truetype_table_find(records, table_count, TAG("maxp"));
        if (offset32_is_valid(maxp_record.offset))
        {
            ByteSlice maxp_content = { .pointer = file.pointer + maxp_record.offset, .length = maxp_record.length };
            let maxp_disk = (MaxpTableCommon*)maxp_content.pointer;
            MaxpTableCommon maxp_memory = {
                .version = bigendian_read_u32_safe(&maxp_disk->version),
                .glyph_count = bigendian_read_u16_safe(&maxp_disk->glyph_count),
            };
            init.information.glyph_count = maxp_memory.glyph_count;
        }
        else
        {
            init.information.glyph_count = UINT16_MAX;
        }

        // Get index_to_loc_format from head table (offset 50)
        {
            let head_offset = init.information.offsets[FONT_INFORMATION_OFFSET_HEAD];
            let head_pointer = file.pointer + head_offset;
            init.information.index_to_loc_format = bigendian_read_u16_safe((u16*)(head_pointer + 50));
        }

        // Find a cmap encoding table we understand
        {
            let cmap_pointer = file.pointer + cmap.offset;
            let cmap_header = (CmapHeader*)cmap_pointer;
            u16 cmap_table_count = bigendian_read_u16_safe(&cmap_header->table_count);
            let encoding_records = (CmapEncodingRecord*)(cmap_header + 1);

            for (u16 i = 0; i < cmap_table_count; i += 1)
            {
                let record = &encoding_records[i];
                let platform_id = bigendian_read_u16_safe(&record->platform_id);
                let encoding_id = bigendian_read_u16_safe(&record->encoding_id);
                let subtable_offset = offset32_read(&record->subtable_offset);

                switch (platform_id)
                {
                    case TTF_PLATFORM_ID_MICROSOFT:
                    {
                        switch (encoding_id)
                        {
                            case TTF_MS_EID_UNICODE_BMP:
                            case TTF_MS_EID_UNICODE_FULL:
                            {
                                // MS/Unicode
                                init.information.cmap_index_map = cmap.offset + subtable_offset;
                            } break;
                            default: break;
                        }
                    } break;
                    case TTF_PLATFORM_ID_UNICODE:
                    {
                        init.information.cmap_index_map = cmap.offset + subtable_offset;
                    } break;
                    default: break;
                }
            }

            if (!offset32_is_valid(init.information.cmap_index_map))
            {
                init.result = TTF_FONT_INITIALIZATION_CORE_TABLES_NOT_PRESENT;
            }
        }

        init.information.file = file;
        init.information.font_offset = font_offset;
    }
    else
    {
        init.result = TTF_FONT_INITIALIZATION_CORE_TABLES_NOT_PRESENT;
    }

    return init;
}

BUSTER_IMPL TtfVerticalMetrics truetype_get_font_vertical_metrics(const TtfFontInformation* info)
{
    let hhea_pointer = info->file.pointer + info->offsets[FONT_INFORMATION_OFFSET_HHEA];
    TtfVerticalMetrics result = {
        .ascent = bigendian_read_s16_safe(hhea_pointer + 4),
        .descent = bigendian_read_s16_safe(hhea_pointer + 6),
        .line_gap = bigendian_read_s16_safe(hhea_pointer + 8),
    };
    return result;
}

BUSTER_IMPL f32 truetype_scale_for_pixel_height(const TtfFontInformation* info, f32 pixels)
{
    let metrics = truetype_get_font_vertical_metrics(info);
    let font_height = (f32)(metrics.ascent - metrics.descent);
    return pixels / font_height;
}

// Find glyph index for a Unicode codepoint using the cmap table
BUSTER_IMPL u32 truetype_find_glyph_index(const TtfFontInformation* info, u32 codepoint)
{
    u32 result = 0;

    let cmap_pointer = info->file.pointer + info->cmap_index_map;
    let format = bigendian_read_u16_safe(cmap_pointer);

    switch (format)
    {
        case 4:
        {
            // Format 4: Segment mapping to delta values
            let segment_count_x2 = bigendian_read_u16_safe(cmap_pointer + 6);
            u16 segment_count = (u16)(segment_count_x2 >> 1);
            let search_range = bigendian_read_u16_safe(cmap_pointer + 8);
            let end_codes = cmap_pointer + 14;
            let start_codes = end_codes + segment_count_x2 + 2;
            let id_deltas = start_codes + segment_count_x2;
            let id_range_offsets = id_deltas + segment_count_x2;

            if (codepoint <= 0xFFFF)
            {
                // Binary search for the segment
                u16 search_start = 0;
                u16 search_end = segment_count;

                if (codepoint >= bigendian_read_u16_safe(end_codes + (search_range - 2)))
                {
                    search_start = (u16)((segment_count_x2 - search_range) >> 1);
                }

                while (search_start < search_end)
                {
                    let mid = (u16)((search_start + search_end) >> 1);
                    let end_code = bigendian_read_u16_safe(end_codes + mid * 2);
                    if (codepoint > end_code)
                    {
                        search_start = (u16)(mid + 1);
                    }
                    else
                    {
                        search_end = mid;
                    }
                }

                let segment_index = search_start;
                let start_code = bigendian_read_u16_safe(start_codes + segment_index * 2);
                let end_code = bigendian_read_u16_safe(end_codes + segment_index * 2);

                if (codepoint >= start_code && codepoint <= end_code)
                {
                    let id_range_offset = bigendian_read_u16_safe(id_range_offsets + segment_index * 2);
                    let id_delta = bigendian_read_s16_safe(id_deltas + segment_index * 2);

                    if (id_range_offset == 0)
                    {
                        result = (u32)((s32)codepoint + id_delta) & 0xFFFF;
                    }
                    else
                    {
                        let glyph_id_address = id_range_offsets + segment_index * 2 + id_range_offset + (codepoint - start_code) * 2;
                        let glyph_id = bigendian_read_u16_safe(glyph_id_address);
                        if (glyph_id != 0)
                        {
                            result = (u32)((s32)glyph_id + id_delta) & 0xFFFF;
                        }
                    }
                }
            }
        } break;

        case 12:
        case 13:
        {
            // Format 12/13: Segmented coverage
            let group_count = bigendian_read_u32_safe(cmap_pointer + 12);
            let groups = cmap_pointer + 16;

            // Binary search
            u32 low = 0;
            u32 high = group_count;
            bool found = false;

            while (low < high && !found)
            {
                let mid = (low + high) >> 1;
                let group = groups + mid * 12;
                let start_char_code = bigendian_read_u32_safe(group);
                let end_char_code = bigendian_read_u32_safe(group + 4);

                if (codepoint < start_char_code)
                {
                    high = mid;
                }
                else if (codepoint > end_char_code)
                {
                    low = mid + 1;
                }
                else
                {
                    let start_glyph_id = bigendian_read_u32_safe(group + 8);
                    if (format == 12)
                    {
                        result = start_glyph_id + (codepoint - start_char_code);
                    }
                    else
                    {
                        // Format 13: all codepoints in range map to same glyph
                        result = start_glyph_id;
                    }
                    found = true;
                }
            }
        } break;

        case 0:
        {
            // Format 0: Byte encoding table (for single-byte encodings)
            if (codepoint < 256)
            {
                result = cmap_pointer[6 + codepoint];
            }
        } break;

        case 6:
        {
            // Format 6: Trimmed table mapping
            let first_code = bigendian_read_u16_safe(cmap_pointer + 6);
            let entry_count = bigendian_read_u16_safe(cmap_pointer + 8);
            if (codepoint >= first_code && codepoint < first_code + entry_count)
            {
                result = bigendian_read_u16_safe(cmap_pointer + 10 + (codepoint - first_code) * 2);
            }
        } break;

        default: break;
    }

    return result;
}

// Get horizontal metrics for a glyph index from the hmtx table
BUSTER_IMPL TtfHorizontalMetrics truetype_get_glyph_horizontal_metrics(const TtfFontInformation* info, u32 glyph_index)
{
    let hhea_pointer = info->file.pointer + info->offsets[FONT_INFORMATION_OFFSET_HHEA];
    let hmtx_pointer = info->file.pointer + info->offsets[FONT_INFORMATION_OFFSET_HMTX];
    let number_of_h_metrics = bigendian_read_u16_safe(hhea_pointer + 34);

    TtfHorizontalMetrics result;

    if (glyph_index < number_of_h_metrics)
    {
        result.advance_width = bigendian_read_s16_safe(hmtx_pointer + glyph_index * 4);
        result.left_side_bearing = bigendian_read_s16_safe(hmtx_pointer + glyph_index * 4 + 2);
    }
    else
    {
        // Use the last advance width, but get left side bearing from extended array
        result.advance_width = bigendian_read_s16_safe(hmtx_pointer + (number_of_h_metrics - 1) * 4);
        result.left_side_bearing = bigendian_read_s16_safe(hmtx_pointer + number_of_h_metrics * 4 + (glyph_index - number_of_h_metrics) * 2);
    }

    return result;
}

// Get horizontal metrics for a Unicode codepoint
BUSTER_IMPL TtfHorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TtfFontInformation* info, u32 codepoint)
{
    let glyph_index = truetype_find_glyph_index(info, codepoint);
    return truetype_get_glyph_horizontal_metrics(info, glyph_index);
}

// Get offset into glyf table for a glyph (returns 0 for empty glyphs like space)
BUSTER_GLOBAL_LOCAL u32 truetype_get_glyph_offset(TtfFontInformation* info, u32 glyph_index)
{
    u32 result = 0;

    let loca_pointer = info->file.pointer + info->offsets[FONT_INFORMATION_OFFSET_LOCA];
    let glyf_offset = info->offsets[FONT_INFORMATION_OFFSET_GLYF];

    u32 g1, g2;
    if (info->index_to_loc_format >= 1)
    {
        // Long format (4 bytes per entry)
        g1 = bigendian_read_u32_safe(loca_pointer + glyph_index * 4);
        g2 = bigendian_read_u32_safe(loca_pointer + glyph_index * 4 + 4);
    }
    else
    {
        // Short format (2 bytes per entry, values are half the actual offset)
        g1 = bigendian_read_u16_safe(loca_pointer + glyph_index * 2) * 2;
        g2 = bigendian_read_u16_safe(loca_pointer + glyph_index * 2 + 2) * 2;
    }

    if (g1 != g2)
    {
        result = glyf_offset + g1;
    }

    return result;
}

STRUCT(TtfBoundingBox)
{
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;
};

// Get glyph bounding box in unscaled coordinates
BUSTER_IMPL bool truetype_get_glyph_box(TtfFontInformation* info, u32 glyph_index, TtfBoundingBox* box)
{
    bool result = false;

    let glyph_offset = truetype_get_glyph_offset(info, glyph_index);
    if (glyph_offset == 0)
    {
        *box = (TtfBoundingBox){};
    }
    else
    {
        let glyph_pointer = info->file.pointer + glyph_offset;
        box->x0 = bigendian_read_s16_safe(glyph_pointer + 2);
        box->y0 = bigendian_read_s16_safe(glyph_pointer + 4);
        box->x1 = bigendian_read_s16_safe(glyph_pointer + 6);
        box->y1 = bigendian_read_s16_safe(glyph_pointer + 8);
        result = true;
    }

    return result;
}

BUSTER_IMPL bool truetype_get_codepoint_box(TtfFontInformation* info, u32 codepoint, TtfBoundingBox* box)
{
    return truetype_get_glyph_box(info, truetype_find_glyph_index(info, codepoint), box);
}

ENUM_T(TtfVertexType, u8,
    TTF_VERTEX_MOVE = 1,
    TTF_VERTEX_LINE = 2,
    TTF_VERTEX_CURVE = 3,
    TTF_VERTEX_CUBIC = 4,
);

STRUCT(TtfVertex)
{
    s16 x;
    s16 y;
    s16 cx;
    s16 cy;
    s16 cx1;
    s16 cy1;
    TtfVertexType type;
    u8 reserved;
};

STRUCT(TtfGlyphShape)
{
    TtfVertex* vertices;
    u32 vertex_count;
    u8 reserved[4];
};

// Close a shape by adding a final segment back to the start
BUSTER_GLOBAL_LOCAL void truetype_close_shape(TtfVertex* vertices, u32* vertex_count, bool was_off, bool start_off,
                                               s32 sx, s32 sy, s32 scx, s32 scy, s32 cx, s32 cy)
{
    if (start_off)
    {
        if (was_off)
        {
            vertices[*vertex_count].type = TTF_VERTEX_CURVE;
            vertices[*vertex_count].x = (s16)((cx + scx) >> 1);
            vertices[*vertex_count].y = (s16)((cy + scy) >> 1);
            vertices[*vertex_count].cx = (s16)cx;
            vertices[*vertex_count].cy = (s16)cy;
            *vertex_count += 1;
        }
        vertices[*vertex_count].type = TTF_VERTEX_CURVE;
        vertices[*vertex_count].x = (s16)sx;
        vertices[*vertex_count].y = (s16)sy;
        vertices[*vertex_count].cx = (s16)scx;
        vertices[*vertex_count].cy = (s16)scy;
        *vertex_count += 1;
    }
    else
    {
        if (was_off)
        {
            vertices[*vertex_count].type = TTF_VERTEX_CURVE;
            vertices[*vertex_count].x = (s16)sx;
            vertices[*vertex_count].y = (s16)sy;
            vertices[*vertex_count].cx = (s16)cx;
            vertices[*vertex_count].cy = (s16)cy;
            *vertex_count += 1;
        }
        else
        {
            vertices[*vertex_count].type = TTF_VERTEX_LINE;
            vertices[*vertex_count].x = (s16)sx;
            vertices[*vertex_count].y = (s16)sy;
            *vertex_count += 1;
        }
    }
}

// Get glyph shape vertices (caller must free vertices with arena or similar)
BUSTER_IMPL TtfGlyphShape truetype_get_glyph_shape(Arena* arena, TtfFontInformation* info, u32 glyph_index)
{
    TtfGlyphShape shape = {};

    let glyph_offset = truetype_get_glyph_offset(info, glyph_index);
    if (glyph_offset != 0)
    {
        let glyph_pointer = info->file.pointer + glyph_offset;
        let contour_count = bigendian_read_s16_safe(glyph_pointer);

        if (contour_count > 0)
        {
            // Simple glyph
            let end_points_of_contours = glyph_pointer + 10;
            let instruction_length = bigendian_read_u16_safe(end_points_of_contours + contour_count * 2);
            let flags_start = end_points_of_contours + contour_count * 2 + 2 + instruction_length;

            u32 point_count = 1 + bigendian_read_u16_safe(end_points_of_contours + (contour_count - 1) * 2);

            // Allocate temporary arrays for flags and points
            u8* flags = arena_allocate(arena, u8, point_count);
            s16* x_coords = arena_allocate(arena, s16, point_count);
            s16* y_coords = arena_allocate(arena, s16, point_count);

            // Parse flags
            u8* flag_pointer = flags_start;
            for (u32 i = 0; i < point_count;)
            {
                let flag = *flag_pointer++;
                flags[i++] = flag;
                if (flag & 8)
                {
                    let repeat_count = *flag_pointer++;
                    for (u32 j = 0; j < repeat_count; j += 1)
                    {
                        flags[i++] = flag;
                    }
                }
            }

            // Parse x coordinates
            s32 x = 0;
            for (u32 i = 0; i < point_count; i += 1)
            {
                let flag = flags[i];
                if (flag & 2)
                {
                    s16 dx = *flag_pointer++;
                    x += (flag & 16) ? dx : -dx;
                }
                else if (!(flag & 16))
                {
                    x += bigendian_read_s16_safe(flag_pointer);
                    flag_pointer += 2;
                }
                x_coords[i] = (s16)x;
            }

            // Parse y coordinates
            s32 y = 0;
            for (u32 i = 0; i < point_count; i += 1)
            {
                let flag = flags[i];
                if (flag & 4)
                {
                    s16 dy = *flag_pointer++;
                    y += (flag & 32) ? dy : -dy;
                }
                else if (!(flag & 32))
                {
                    y += bigendian_read_s16_safe(flag_pointer);
                    flag_pointer += 2;
                }
                y_coords[i] = (s16)y;
            }

            // Allocate vertices (worst case: 2 vertices per point + 1 per contour for close)
            u32 max_vertices = point_count * 2 + (u32)contour_count;
            shape.vertices = arena_allocate(arena, TtfVertex, max_vertices);

            u32 vertex_count = 0;
            u32 next_contour_end = bigendian_read_u16_safe(end_points_of_contours);
            s32 contour_index = 0;
            u32 point_index = 0;

            while (point_index < point_count)
            {
                // Find start of this contour
                u32 contour_start = point_index;
                u32 contour_end = next_contour_end;

                // Track starting point info
                s32 sx = x_coords[contour_start];
                s32 sy = y_coords[contour_start];
                s32 scx = 0, scy = 0;
                s32 cx = 0, cy = 0;
                bool start_off = (flags[contour_start] & 1) == 0;
                bool was_off = false;

                if (start_off)
                {
                    // First point is off-curve
                    scx = sx;
                    scy = sy;
                    if ((flags[contour_end] & 1) == 0)
                    {
                        // Both first and last are off-curve, start at midpoint
                        sx = (sx + x_coords[contour_end]) >> 1;
                        sy = (sy + y_coords[contour_end]) >> 1;
                    }
                    else
                    {
                        // Start at last point
                        sx = x_coords[contour_end];
                        sy = y_coords[contour_end];
                    }
                    point_index += 1;
                }

                // Start the contour
                shape.vertices[vertex_count].type = TTF_VERTEX_MOVE;
                shape.vertices[vertex_count].x = (s16)sx;
                shape.vertices[vertex_count].y = (s16)sy;
                vertex_count += 1;

                // Process points in this contour
                for (; point_index <= contour_end; point_index += 1)
                {
                    s32 px = x_coords[point_index];
                    s32 py = y_coords[point_index];
                    bool on_curve = (flags[point_index] & 1) != 0;

                    if (on_curve)
                    {
                        if (was_off)
                        {
                            shape.vertices[vertex_count].type = TTF_VERTEX_CURVE;
                            shape.vertices[vertex_count].x = (s16)px;
                            shape.vertices[vertex_count].y = (s16)py;
                            shape.vertices[vertex_count].cx = (s16)cx;
                            shape.vertices[vertex_count].cy = (s16)cy;
                            vertex_count += 1;
                        }
                        else
                        {
                            shape.vertices[vertex_count].type = TTF_VERTEX_LINE;
                            shape.vertices[vertex_count].x = (s16)px;
                            shape.vertices[vertex_count].y = (s16)py;
                            vertex_count += 1;
                        }
                        was_off = false;
                    }
                    else
                    {
                        if (was_off)
                        {
                            // Two off-curve points in a row: insert implied on-curve point
                            shape.vertices[vertex_count].type = TTF_VERTEX_CURVE;
                            shape.vertices[vertex_count].x = (s16)((cx + px) >> 1);
                            shape.vertices[vertex_count].y = (s16)((cy + py) >> 1);
                            shape.vertices[vertex_count].cx = (s16)cx;
                            shape.vertices[vertex_count].cy = (s16)cy;
                            vertex_count += 1;
                        }
                        cx = px;
                        cy = py;
                        was_off = true;
                    }
                }

                // Close the contour
                truetype_close_shape(shape.vertices, &vertex_count, was_off, start_off, sx, sy, scx, scy, cx, cy);

                // Move to next contour
                contour_index += 1;
                if (contour_index < contour_count)
                {
                    next_contour_end = bigendian_read_u16_safe(end_points_of_contours + contour_index * 2);
                }
            }

            shape.vertex_count = vertex_count;
        }
        else if (contour_count < 0)
        {
            // Composite glyph - TODO: implement composite glyph support
            // For now, return empty shape
        }
    }

    return shape;
}

BUSTER_IMPL TtfGlyphShape truetype_get_codepoint_shape(Arena* arena, TtfFontInformation* info, u32 codepoint)
{
    return truetype_get_glyph_shape(arena, info, truetype_find_glyph_index(info, codepoint));
}

// Flatten curves into line segments for rasterization
STRUCT(TtfEdge)
{
    f32 x0, y0, x1, y1;
    bool invert;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL void truetype_add_edge(TtfEdge* edges, u32* edge_count, f32 x0, f32 y0, f32 x1, f32 y1)
{
    // Skip horizontal edges
    if (y0 != y1)
    {
        TtfEdge* e = &edges[*edge_count];
        if (y0 < y1)
        {
            e->x0 = x0;
            e->y0 = y0;
            e->x1 = x1;
            e->y1 = y1;
            e->invert = false;
        }
        else
        {
            e->x0 = x1;
            e->y0 = y1;
            e->x1 = x0;
            e->y1 = y0;
            e->invert = true;
        }
        *edge_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void truetype_tessellate_curve(TtfEdge* edges, u32* edge_count,
                                                    f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2,
                                                    f32 objspace_flatness_squared, s32 depth)
{
    if (depth <= 16)
    {
        f32 mx = (x0 + 2*x1 + x2) / 4;
        f32 my = (y0 + 2*y1 + y2) / 4;
        f32 dx = (x0 + x2) / 2 - mx;
        f32 dy = (y0 + y2) / 2 - my;

        if (dx*dx + dy*dy > objspace_flatness_squared)
        {
            truetype_tessellate_curve(edges, edge_count, x0, y0, (x0+x1)/2, (y0+y1)/2, mx, my, objspace_flatness_squared, depth+1);
            truetype_tessellate_curve(edges, edge_count, mx, my, (x1+x2)/2, (y1+y2)/2, x2, y2, objspace_flatness_squared, depth+1);
        }
        else
        {
            truetype_add_edge(edges, edge_count, x0, y0, x2, y2);
        }
    }
}

BUSTER_GLOBAL_LOCAL s32 truetype_edge_compare(const void* a, const void* b)
{
    let ea = (TtfEdge*)a;
    let eb = (TtfEdge*)b;

    s32 result = 0;
    if (ea->y0 < eb->y0)
    {
        result = -1;
    }
    else if (ea->y0 > eb->y0)
    {
        result = 1;
    }

    return result;
}

// Rasterize edges into bitmap using scanline algorithm
BUSTER_GLOBAL_LOCAL void truetype_rasterize_edges(u8* pixels, s32 width, s32 height, TtfEdge* edges, u32 edge_count, Arena* arena)
{
    if (edge_count != 0)
    {
        // Sort edges by y0
        qsort(edges, edge_count, sizeof(TtfEdge), truetype_edge_compare);

        // Allocate scanline buffer
        f32* scanline = arena_allocate(arena, f32, (u64)(width + 1));

        // Active edge list
        TtfEdge** active = arena_allocate(arena, TtfEdge*, edge_count);
        u32 active_count = 0;
        u32 edge_index = 0;

        for (s32 y = 0; y < height; y += 1)
        {
            f32 scan_y = (f32)y + 0.5f;

            // Add new edges that start at or before this scanline
            while (edge_index < edge_count && edges[edge_index].y0 <= scan_y)
            {
                active[active_count++] = &edges[edge_index];
                edge_index += 1;
            }

            // Remove edges that end before this scanline
            for (u32 i = 0; i < active_count;)
            {
                if (active[i]->y1 <= scan_y)
                {
                    active[i] = active[--active_count];
                }
                else
                {
                    i += 1;
                }
            }

            // Clear scanline
            memset(scanline, 0, sizeof(f32) * (u64)(width + 1));

            // Calculate coverage for each active edge
            for (u32 i = 0; i < active_count; i += 1)
            {
                let e = active[i];
                f32 dy = e->y1 - e->y0;
                f32 dx = e->x1 - e->x0;
                f32 dxdy = dx / dy;

                // Calculate x at this scanline
                f32 x = e->x0 + dxdy * (scan_y - e->y0);
                f32 direction = e->invert ? -1.0f : 1.0f;

                s32 xi = (s32)x;
                if (xi < 0) xi = 0;
                if (xi >= width) xi = width - 1;

                // Add winding contribution
                scanline[xi] += direction;
            }

            // Convert winding to coverage and write pixels
            f32 sum = 0;
            for (s32 x = 0; x < width; x += 1)
            {
                sum += scanline[x];
                f32 coverage = sum;
                if (coverage < 0) coverage = -coverage;
                if (coverage > 1) coverage = 1;

                s32 value = (s32)(coverage * 255.0f + 0.5f);
                if (value > 255) value = 255;
                pixels[y * width + x] = (u8)value;
            }
        }
    }
}

BUSTER_IMPL TtfBitmap truetype_get_glyph_bitmap(Arena* arena, TtfFontInformation* info, f32 scale_x, f32 scale_y, u32 glyph_index)
{
    TtfBitmap result = {};

    TtfBoundingBox box;
    if (truetype_get_glyph_box(info, glyph_index, &box))
    {
        // Calculate bitmap dimensions
        result.x_offset = (s32)(box.x0 * scale_x);
        result.y_offset = (s32)(-box.y1 * scale_y);
        result.width = (s32)(box.x1 * scale_x) - result.x_offset + 1;
        result.height = (s32)(-box.y0 * scale_y) - result.y_offset + 1;

        if (result.width > 0 && result.height > 0)
        {
            // Get glyph shape
            TtfGlyphShape shape = truetype_get_glyph_shape(arena, info, glyph_index);
            if (shape.vertex_count == 0)
            {
                result.width = 0;
                result.height = 0;
            }
            else
            {
                // Allocate edges (worst case: many edges per curve)
                u32 max_edges = shape.vertex_count * 20;
                TtfEdge* edges = arena_allocate(arena, TtfEdge, max_edges);
                u32 edge_count = 0;

                // Flatten curves into edges
                f32 flatness = 0.35f / scale_x;
                f32 flatness_squared = flatness * flatness;
                f32 x = 0, y = 0;

                for (u32 i = 0; i < shape.vertex_count; i += 1)
                {
                    let v = &shape.vertices[i];
                    switch (v->type)
                    {
                        case TTF_VERTEX_MOVE:
                        {
                            x = (f32)v->x;
                            y = (f32)v->y;
                        } break;
                        case TTF_VERTEX_LINE:
                        {
                            truetype_add_edge(edges, &edge_count, x * scale_x, -y * scale_y, v->x * scale_x, -v->y * scale_y);
                            x = (f32)v->x;
                            y = (f32)v->y;
                        } break;
                        case TTF_VERTEX_CURVE:
                        {
                            truetype_tessellate_curve(edges, &edge_count,
                                x * scale_x, -y * scale_y,
                                v->cx * scale_x, -v->cy * scale_y,
                                v->x * scale_x, -v->y * scale_y,
                                flatness_squared, 0);
                            x = (f32)v->x;
                            y = (f32)v->y;
                        } break;
                        case TTF_VERTEX_CUBIC:
                        {
                            // Cubic curves not supported in basic TrueType, skip
                            x = (f32)v->x;
                            y = (f32)v->y;
                        } break;
                    }
                }

                // Shift edges to bitmap origin
                for (u32 i = 0; i < edge_count; i += 1)
                {
                    edges[i].x0 -= (f32)result.x_offset;
                    edges[i].y0 -= (f32)result.y_offset;
                    edges[i].x1 -= (f32)result.x_offset;
                    edges[i].y1 -= (f32)result.y_offset;
                }

                // Allocate and rasterize
                result.pixels = arena_allocate(arena, u8, (u64)(result.width * result.height));
                memset(result.pixels, 0, (u64)(result.width * result.height));
                truetype_rasterize_edges(result.pixels, result.width, result.height, edges, edge_count, arena);
            }
        }
        else
        {
            result.width = 0;
            result.height = 0;
        }
    }

    return result;
}

BUSTER_IMPL TtfBitmap truetype_get_codepoint_bitmap(Arena* arena, TtfFontInformation* info, f32 scale_x, f32 scale_y, u32 codepoint)
{
    return truetype_get_glyph_bitmap(arena, info, scale_x, scale_y, truetype_find_glyph_index(info, codepoint));
}

BUSTER_IMPL s32 truetype_get_glyph_kern_advance(const TtfFontInformation* info, u32 glyph1, u32 glyph2)
{
    s32 result = 0;

    if (offset32_is_valid(info->offsets[FONT_INFORMATION_OFFSET_KERN]))
    {
        let kern_pointer = info->file.pointer + info->offsets[FONT_INFORMATION_OFFSET_KERN];
        let version = bigendian_read_u16_safe(kern_pointer);
        bool found = false;

        if (version == 0)
        {
            // Microsoft/OpenType kern table format
            let subtable_count = bigendian_read_u16_safe(kern_pointer + 2);
            let subtable = kern_pointer + 4;

            for (u16 i = 0; i < subtable_count && !found; i += 1)
            {
                let subtable_version = bigendian_read_u16_safe(subtable);
                let subtable_length = bigendian_read_u16_safe(subtable + 2);
                let coverage = bigendian_read_u16_safe(subtable + 4);

                // Check if this is horizontal kerning (coverage bit 0) and format 0 (coverage bits 8-15)
                let format = (coverage >> 8) & 0xFF;
                let is_horizontal = (coverage & 1) != 0;
                let is_minimum = (coverage & 2) != 0;
                let is_override = (coverage & 8) != 0;

                if (format == 0 && is_horizontal && !is_minimum && !is_override)
                {
                    let pair_count = bigendian_read_u16_safe(subtable + 6);
                    let pairs = subtable + 14;

                    // Binary search for the glyph pair
                    u32 needle = (glyph1 << 16) | glyph2;
                    s32 low = 0;
                    s32 high = pair_count - 1;

                    while (low <= high && !found)
                    {
                        s32 mid = (low + high) >> 1;
                        let pair = pairs + mid * 6;
                        u32 pair_key = (u32)(bigendian_read_u16_safe(pair) << 16) | bigendian_read_u16_safe(pair + 2);

                        if (needle < pair_key)
                        {
                            high = mid - 1;
                        }
                        else if (needle > pair_key)
                        {
                            low = mid + 1;
                        }
                        else
                        {
                            result = bigendian_read_s16_safe(pair + 4);
                            found = true;
                        }
                    }
                }

                subtable += subtable_length;
                BUSTER_UNUSED(subtable_version);
            }
        }
        else if (version == 1)
        {
            // Apple kern table format (version is actually 0x00010000 as fixed-point)
            // Re-read as 32-bit version
            u32 apple_version = bigendian_read_u32_safe((u32*)kern_pointer);
            if (apple_version == 0x00010000)
            {
                let subtable_count = bigendian_read_u32_safe(kern_pointer + 4);
                let subtable = kern_pointer + 8;

                for (u32 i = 0; i < subtable_count && !found; i += 1)
                {
                    let subtable_length = bigendian_read_u32_safe(subtable);
                    let coverage = bigendian_read_u16_safe(subtable + 4);
                    let format = coverage & 0xFF;
                    let is_horizontal = (coverage & 0x8000) == 0;

                    if (format == 0 && is_horizontal)
                    {
                        let pair_count = bigendian_read_u16_safe(subtable + 8);
                        let pairs = subtable + 16;

                        // Binary search
                        u32 needle = (glyph1 << 16) | glyph2;
                        s32 low = 0;
                        s32 high = pair_count - 1;

                        while (low <= high && !found)
                        {
                            s32 mid = (low + high) >> 1;
                            let pair = pairs + mid * 6;
                            u32 pair_key = (u32)(bigendian_read_u16_safe(pair) << 16) | bigendian_read_u16_safe(pair + 2);

                            if (needle < pair_key)
                            {
                                high = mid - 1;
                            }
                            else if (needle > pair_key)
                            {
                                low = mid + 1;
                            }
                            else
                            {
                                result = bigendian_read_s16_safe(pair + 4);
                                found = true;
                            }
                        }
                    }

                    subtable += subtable_length;
                }
            }
        }
    }

    return result;
}

BUSTER_IMPL s32 truetype_get_codepoint_kern_advance(TtfFontInformation* info, u32 codepoint1, u32 codepoint2)
{
    let glyph1 = truetype_find_glyph_index(info, codepoint1);
    let glyph2 = truetype_find_glyph_index(info, codepoint2);
    return truetype_get_glyph_kern_advance(info, glyph1, glyph2);
}
