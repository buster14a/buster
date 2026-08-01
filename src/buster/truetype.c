#include <buster/truetype.h>
#include <buster/float.h>
#include <buster/string.h>

#define TTF_GLYPH_RECURSION_LIMIT 8u
#define TTF_CURVE_SEGMENTS 12u
#define TTF_RASTER_SUBSAMPLES 4u

typedef struct TTF_TableRecord TTF_TableRecord;
struct TTF_TableRecord
{
    u64 offset;
    u64 length;
};

typedef struct TTF_GlyphRange TTF_GlyphRange;
struct TTF_GlyphRange
{
    u64 offset;
    u64 length;
};

typedef struct TTF_Point TTF_Point;
struct TTF_Point
{
    f32 x;
    f32 y;
    bool on_curve;
    u8 reserved[3];
};

typedef struct TTF_RasterPoint TTF_RasterPoint;
struct TTF_RasterPoint
{
    f32 x;
    f32 y;
};

typedef struct TTF_RasterPath TTF_RasterPath;
struct TTF_RasterPath
{
    TTF_RasterPoint* points;
    u32* contour_ends;
    u32 point_count;
    u32 contour_count;
    u32 point_capacity;
    u32 contour_capacity;
    bool overflowed;
    u8 reserved[3];
};

typedef struct TTF_Transform TTF_Transform;
struct TTF_Transform
{
    f32 m00;
    f32 m01;
    f32 m10;
    f32 m11;
    f32 dx;
    f32 dy;
};

BUSTER_GLOBAL_LOCAL bool ttf_range_is_valid(ByteSlice data, u64 offset, u64 length)
{
    bool result = offset <= data.length && length <= data.length - offset;
    return result;
}

BUSTER_GLOBAL_LOCAL u8 ttf_u8(ByteSlice data, u64 offset)
{
    u8 result = 0;
    if (ttf_range_is_valid(data, offset, 1))
    {
        result = data.pointer[offset];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u16 ttf_u16(ByteSlice data, u64 offset)
{
    u16 result = 0;
    if (ttf_range_is_valid(data, offset, 2))
    {
        result = (u16)(((u16)data.pointer[offset] << 8u) | (u16)data.pointer[offset + 1]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s16 ttf_s16(ByteSlice data, u64 offset)
{
    u16 value = ttf_u16(data, offset);
    s32 signed_value = value >= 0x8000u ? (s32)value - 0x10000 : (s32)value;
    s16 result = (s16)signed_value;
    return result;
}

BUSTER_GLOBAL_LOCAL s32 ttf_s8_as_s32(ByteSlice data, u64 offset)
{
    u8 value = ttf_u8(data, offset);
    s32 result = value >= 0x80u ? (s32)value - 0x100 : (s32)value;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 ttf_u32(ByteSlice data, u64 offset)
{
    u32 result = 0;
    if (ttf_range_is_valid(data, offset, 4))
    {
        result =
            ((u32)data.pointer[offset] << 24u) | ((u32)data.pointer[offset + 1] << 16u) | ((u32)data.pointer[offset + 2] << 8u) | (u32)data.pointer[offset + 3];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ttf_tag_equal(ByteSlice data, u64 offset, char8 a, char8 b, char8 c, char8 d)
{
    bool result = ttf_range_is_valid(data, offset, 4) && data.pointer[offset + 0] == (u8)a && data.pointer[offset + 1] == (u8)b &&
                  data.pointer[offset + 2] == (u8)c && data.pointer[offset + 3] == (u8)d;
    return result;
}

BUSTER_GLOBAL_LOCAL TTF_TableRecord ttf_find_table(ByteSlice data, u64 font_start, char8 a, char8 b, char8 c, char8 d)
{
    TTF_TableRecord result = {0};
    if (ttf_range_is_valid(data, font_start, 12))
    {
        u16 table_count = ttf_u16(data, font_start + 4);
        u64 records = font_start + 12;
        for (u32 table_index = 0; table_index < (u32)table_count; table_index += 1)
        {
            u64 record = records + (u64)table_index * 16u;
            if (!ttf_range_is_valid(data, record, 16))
            {
                break;
            }
            if (ttf_tag_equal(data, record, a, b, c, d))
            {
                u64 offset = (u64)ttf_u32(data, record + 8);
                u64 length = (u64)ttf_u32(data, record + 12);
                if (ttf_range_is_valid(data, offset, length))
                {
                    result.offset = offset;
                    result.length = length;
                }
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ttf_table_exists(TTF_TableRecord record)
{
    bool result = record.offset != 0 || record.length != 0;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ttf_font_offset_for_index(ByteSlice data, u32 font_index)
{
    u64 result = 0;
    if (ttf_tag_equal(data, 0, 't', 't', 'c', 'f'))
    {
        u32 count = ttf_u32(data, 8);
        if (font_index < count && ttf_range_is_valid(data, 12u + (u64)font_index * 4u, 4))
        {
            result = (u64)ttf_u32(data, 12u + (u64)font_index * 4u);
        }
    }
    else if (font_index == 0)
    {
        result = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ttf_find_unicode_cmap(ByteSlice data, TTF_TableRecord cmap, u64* subtable, u32* format)
{
    bool result = false;
    if (ttf_range_is_valid(data, cmap.offset, 4))
    {
        u16 encoding_count = ttf_u16(data, cmap.offset + 2);
        u64 records = cmap.offset + 4;
        u64 best = 0;
        u32 best_format = 0;
        u32 best_score = 0;
        for (u32 encoding_index = 0; encoding_index < (u32)encoding_count; encoding_index += 1)
        {
            u64 record = records + (u64)encoding_index * 8u;
            if (!ttf_range_is_valid(data, record, 8))
            {
                break;
            }
            u16 platform_id = ttf_u16(data, record + 0);
            u16 encoding_id = ttf_u16(data, record + 2);
            u64 offset = cmap.offset + (u64)ttf_u32(data, record + 4);
            if (!ttf_range_is_valid(data, offset, 2))
            {
                continue;
            }
            u32 candidate_format = (u32)ttf_u16(data, offset);
            bool supported_format = candidate_format == 4u || candidate_format == 12u;
            bool unicode_encoding = platform_id == 0u || (platform_id == 3u && (encoding_id == 1u || encoding_id == 10u));
            if (supported_format && unicode_encoding)
            {
                u32 score = (candidate_format == 12u ? 100u : 10u) + (platform_id == 3u ? 2u : 1u);
                if (score > best_score)
                {
                    best_score = score;
                    best = offset;
                    best_format = candidate_format;
                }
            }
        }
        if (best_score != 0)
        {
            *subtable = best;
            *format = best_format;
            result = true;
        }
    }
    return result;
}

TTF_FontInitialization truetype_font_initialize(ByteSlice file, u32 font_index)
{
    TTF_FontInitialization result = {0};
    u64 font_start = ttf_font_offset_for_index(file, font_index);
    if (!ttf_range_is_valid(file, font_start, 12))
    {
        string_print(S8("ttf range not valid"));
        result.result = TTF_FONT_INITIALIZATION_FAILED;
        return result;
    }

    u32 sfnt_version = ttf_u32(file, font_start);
    bool supported_sfnt = sfnt_version == 0x00010000u || ttf_tag_equal(file, font_start, 't', 'r', 'u', 'e');
    if (!supported_sfnt)
    {
        string_print(S8("not supported_sfnt"));
        result.result = TTF_FONT_INITIALIZATION_UNSUPPORTED;
        return result;
    }

    TTF_TableRecord cmap = ttf_find_table(file, font_start, 'c', 'm', 'a', 'p');
    TTF_TableRecord loca = ttf_find_table(file, font_start, 'l', 'o', 'c', 'a');
    TTF_TableRecord head = ttf_find_table(file, font_start, 'h', 'e', 'a', 'd');
    TTF_TableRecord glyf = ttf_find_table(file, font_start, 'g', 'l', 'y', 'f');
    TTF_TableRecord hhea = ttf_find_table(file, font_start, 'h', 'h', 'e', 'a');
    TTF_TableRecord hmtx = ttf_find_table(file, font_start, 'h', 'm', 't', 'x');
    TTF_TableRecord maxp = ttf_find_table(file, font_start, 'm', 'a', 'x', 'p');
    TTF_TableRecord kern = ttf_find_table(file, font_start, 'k', 'e', 'r', 'n');

    bool have_cmap = ttf_table_exists(cmap);
    bool have_loca = ttf_table_exists(loca);
    bool have_head = ttf_table_exists(head);
    bool have_glyf = ttf_table_exists(glyf);
    bool have_hhea = ttf_table_exists(hhea);
    bool have_hmtx = ttf_table_exists(hmtx);
    bool have_maxp = ttf_table_exists(maxp);

    if (!have_cmap || !have_loca || !have_head || !have_glyf || !have_hhea || !have_hmtx || !have_maxp)
    {
        string_print(S8("cmap: {u32}. loca: {u32}. head: {u32}. glyf: {u32}. hhea: {u32}. hmtx: {u32}. maxp: {u32}\n"), have_cmap, have_loca, have_head,
                     have_glyf, have_hhea, have_hmtx, have_maxp);
        result.result = TTF_FONT_INITIALIZATION_FAILED;
        return result;
    }

    u64 cmap_subtable = 0;
    u32 cmap_format = 0;
    if (!ttf_find_unicode_cmap(file, cmap, &cmap_subtable, &cmap_format))
    {
        string_print(S8("ttf_find_unicode_cmap failed"));
        result.result = TTF_FONT_INITIALIZATION_UNSUPPORTED;
        return result;
    }

    result.information = (TTF_FontInformation){
        .data = file,
        .font_start = font_start,
        .cmap = cmap.offset,
        .cmap_subtable = cmap_subtable,
        .loca = loca.offset,
        .head = head.offset,
        .glyf = glyf.offset,
        .hhea = hhea.offset,
        .hmtx = hmtx.offset,
        .kern = kern.offset,
        .cmap_format = cmap_format,
        .units_per_em = ttf_u16(file, head.offset + 18),
        .num_glyphs = ttf_u16(file, maxp.offset + 4),
        .num_hmetrics = ttf_u16(file, hhea.offset + 34),
        .index_to_loc_format = (s32)ttf_s16(file, head.offset + 50),
        .max_points = ttf_u16(file, maxp.offset + 6),
        .max_contours = ttf_u16(file, maxp.offset + 8),
        .max_composite_points = ttf_u16(file, maxp.offset + 10),
        .max_composite_contours = ttf_u16(file, maxp.offset + 12),
    };
    result.result = TTF_FONT_INITIALIZATION_SUCCESS;
    return result;
}

f32 truetype_scale_for_pixel_height(const TTF_FontInformation* information, f32 height)
{
    s32 ascent = (s32)ttf_s16(information->data, information->hhea + 4);
    s32 descent = (s32)ttf_s16(information->data, information->hhea + 6);
    s32 em_height = ascent - descent;
    f32 result = em_height != 0 ? height / (f32)em_height : 0.0f;
    return result;
}

TTF_VerticalMetrics truetype_get_font_vertical_metrics(const TTF_FontInformation* information)
{
    TTF_VerticalMetrics result = {
        .ascent = (s32)ttf_s16(information->data, information->hhea + 4),
        .descent = (s32)ttf_s16(information->data, information->hhea + 6),
        .line_gap = (s32)ttf_s16(information->data, information->hhea + 8),
    };
    return result;
}

BUSTER_GLOBAL_LOCAL u32 truetype_glyph_index_from_codepoint(const TTF_FontInformation* information, u32 codepoint)
{
    ByteSlice data = information->data;
    u64 index_map = information->cmap_subtable;
    u32 result = 0;

    if (information->cmap_format == 4u)
    {
        u16 seg_count = (u16)(ttf_u16(data, index_map + 6) / 2u);
        u64 end_count = index_map + 14;
        u64 start_count = end_count + (u64)seg_count * 2u + 2u;
        u64 id_delta = start_count + (u64)seg_count * 2u;
        u64 id_range_offset = id_delta + (u64)seg_count * 2u;
        for (u32 segment = 0; segment < (u32)seg_count; segment += 1)
        {
            u32 end_code = (u32)ttf_u16(data, end_count + (u64)segment * 2u);
            if (codepoint > end_code)
            {
                continue;
            }
            u32 start_code = (u32)ttf_u16(data, start_count + (u64)segment * 2u);
            if (codepoint < start_code)
            {
                break;
            }
            s32 delta = (s32)ttf_s16(data, id_delta + (u64)segment * 2u);
            u16 range_offset = ttf_u16(data, id_range_offset + (u64)segment * 2u);
            if (range_offset == 0)
            {
                result = (u32)((codepoint + (u32)delta) & 0xffffu);
            }
            else
            {
                u64 glyph_offset = id_range_offset + (u64)segment * 2u + (u64)range_offset + (u64)(codepoint - start_code) * 2u;
                u32 glyph = (u32)ttf_u16(data, glyph_offset);
                if (glyph != 0)
                {
                    glyph = (u32)((glyph + (u32)delta) & 0xffffu);
                }
                result = glyph;
            }
            break;
        }
    }
    else if (information->cmap_format == 12u)
    {
        u32 group_count = ttf_u32(data, index_map + 12);
        u64 groups = index_map + 16;
        for (u32 group = 0; group < group_count; group += 1)
        {
            u64 offset = groups + (u64)group * 12u;
            if (!ttf_range_is_valid(data, offset, 12))
            {
                break;
            }
            u32 start_char = ttf_u32(data, offset + 0);
            u32 end_char = ttf_u32(data, offset + 4);
            if (codepoint < start_char)
            {
                break;
            }
            if (codepoint <= end_char)
            {
                u32 start_glyph = ttf_u32(data, offset + 8);
                result = start_glyph + (codepoint - start_char);
                break;
            }
        }
    }

    if (result >= (u32)information->num_glyphs)
    {
        result = 0;
    }
    return result;
}

TTF_HorizontalMetrics truetype_get_codepoint_horizontal_metrics(const TTF_FontInformation* information, u32 codepoint)
{
    ByteSlice data = information->data;
    u32 glyph = truetype_glyph_index_from_codepoint(information, codepoint);
    u32 hmetrics = (u32)information->num_hmetrics;
    TTF_HorizontalMetrics result = {0};
    if (hmetrics == 0)
    {
        return result;
    }

    if (glyph < hmetrics)
    {
        u64 offset = information->hmtx + (u64)glyph * 4u;
        result.advance_width = (s32)ttf_u16(data, offset + 0);
        result.left_side_bearing = (s32)ttf_s16(data, offset + 2);
    }
    else
    {
        u64 advance_offset = information->hmtx + (u64)(hmetrics - 1u) * 4u;
        u64 lsb_offset = information->hmtx + (u64)hmetrics * 4u + (u64)(glyph - hmetrics) * 2u;
        result.advance_width = (s32)ttf_u16(data, advance_offset + 0);
        result.left_side_bearing = (s32)ttf_s16(data, lsb_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL TTF_GlyphRange truetype_glyph_range(const TTF_FontInformation* information, u32 glyph)
{
    ByteSlice data = information->data;
    TTF_GlyphRange result = {0};
    if (glyph >= (u32)information->num_glyphs)
    {
        return result;
    }

    u64 start = 0;
    u64 end = 0;
    if (information->index_to_loc_format == 0)
    {
        start = (u64)ttf_u16(data, information->loca + (u64)glyph * 2u) * 2u;
        end = (u64)ttf_u16(data, information->loca + (u64)(glyph + 1u) * 2u) * 2u;
    }
    else
    {
        start = (u64)ttf_u32(data, information->loca + (u64)glyph * 4u);
        end = (u64)ttf_u32(data, information->loca + (u64)(glyph + 1u) * 4u);
    }

    if (end >= start && ttf_range_is_valid(data, information->glyf + start, end - start))
    {
        result.offset = information->glyf + start;
        result.length = end - start;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s32 truetype_get_glyph_kern_advance(const TTF_FontInformation* information, u32 glyph_left, u32 glyph_right)
{
    ByteSlice data = information->data;
    s32 result = 0;
    if (information->kern == 0 || !ttf_range_is_valid(data, information->kern, 4))
    {
        return result;
    }

    u32 table_count = (u32)ttf_u16(data, information->kern + 2);
    u64 subtable = information->kern + 4;
    for (u32 table = 0; table < table_count; table += 1)
    {
        if (!ttf_range_is_valid(data, subtable, 6))
        {
            break;
        }
        u32 length = (u32)ttf_u16(data, subtable + 2);
        u16 coverage = ttf_u16(data, subtable + 4);
        u32 format = (u32)(coverage >> 8u);
        bool horizontal = (coverage & 1u) != 0;
        if (format == 0u && horizontal && length >= 14u && ttf_range_is_valid(data, subtable, (u64)length))
        {
            u32 pair_count = (u32)ttf_u16(data, subtable + 6);
            u64 pairs = subtable + 14;
            u32 needle = (glyph_left << 16u) | glyph_right;
            u32 low = 0;
            u32 high = pair_count;
            while (low < high)
            {
                u32 mid = low + (high - low) / 2u;
                u64 pair = pairs + (u64)mid * 6u;
                u32 straw = ((u32)ttf_u16(data, pair + 0) << 16u) | (u32)ttf_u16(data, pair + 2);
                if (needle < straw)
                {
                    high = mid;
                }
                else if (needle > straw)
                {
                    low = mid + 1u;
                }
                else
                {
                    result = (s32)ttf_s16(data, pair + 4);
                    return result;
                }
            }
        }
        if (length == 0)
        {
            break;
        }
        subtable += (u64)length;
    }
    return result;
}

s32 truetype_get_codepoint_kern_advance(const TTF_FontInformation* information, u32 codepoint_left, u32 codepoint_right)
{
    u32 glyph_left = truetype_glyph_index_from_codepoint(information, codepoint_left);
    u32 glyph_right = truetype_glyph_index_from_codepoint(information, codepoint_right);
    s32 result = truetype_get_glyph_kern_advance(information, glyph_left, glyph_right);
    return result;
}

BUSTER_GLOBAL_LOCAL TTF_Point ttf_transform_point(TTF_Transform transform, s32 x, s32 y)
{
    f32 source_x = (f32)x;
    f32 source_y = (f32)y;
    TTF_Point result = {
        .x = transform.m00 * source_x + transform.m10 * source_y + transform.dx,
        .y = transform.m01 * source_x + transform.m11 * source_y + transform.dy,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL TTF_RasterPoint ttf_pixel_point(TTF_Point point, f32 scale_x, f32 scale_y, s32 x0, s32 y0)
{
    TTF_RasterPoint result = {
        .x = point.x * scale_x - (f32)x0,
        .y = -point.y * scale_y - (f32)y0,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL void ttf_raster_path_add_point(TTF_RasterPath* path, TTF_RasterPoint point)
{
    if (path->point_count < path->point_capacity)
    {
        path->points[path->point_count] = point;
        path->point_count += 1;
    }
    else
    {
        path->overflowed = true;
    }
}

BUSTER_GLOBAL_LOCAL void ttf_raster_path_end_contour(TTF_RasterPath* path)
{
    if (path->contour_count < path->contour_capacity)
    {
        path->contour_ends[path->contour_count] = path->point_count;
        path->contour_count += 1;
    }
    else
    {
        path->overflowed = true;
    }
}

BUSTER_GLOBAL_LOCAL TTF_Point ttf_midpoint(TTF_Point a, TTF_Point b)
{
    TTF_Point result = {
        .x = (a.x + b.x) * 0.5f,
        .y = (a.y + b.y) * 0.5f,
        .on_curve = true,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL bool ttf_points_equal(TTF_Point a, TTF_Point b)
{
    bool result = a.x == b.x && a.y == b.y;
    return result;
}

BUSTER_GLOBAL_LOCAL void ttf_append_line(TTF_RasterPath* path, TTF_Point to, f32 scale_x, f32 scale_y, s32 x0, s32 y0)
{
    ttf_raster_path_add_point(path, ttf_pixel_point(to, scale_x, scale_y, x0, y0));
}

BUSTER_GLOBAL_LOCAL void ttf_append_quadratic(TTF_RasterPath* path, TTF_Point from, TTF_Point control, TTF_Point to, f32 scale_x, f32 scale_y, s32 x0, s32 y0)
{
    for (u32 segment = 1; segment <= TTF_CURVE_SEGMENTS; segment += 1)
    {
        f32 t = (f32)segment / (f32)TTF_CURVE_SEGMENTS;
        f32 omt = 1.0f - t;
        TTF_Point point = {
            .x = omt * omt * from.x + 2.0f * omt * t * control.x + t * t * to.x,
            .y = omt * omt * from.y + 2.0f * omt * t * control.y + t * t * to.y,
            .on_curve = true,
        };
        ttf_raster_path_add_point(path, ttf_pixel_point(point, scale_x, scale_y, x0, y0));
    }
}

BUSTER_GLOBAL_LOCAL bool ttf_append_glyph_path(Arena* arena, const TTF_FontInformation* information, u32 glyph, TTF_Transform transform, f32 scale_x,
                                               f32 scale_y, s32 x0, s32 y0, u32 recursion_depth, TTF_RasterPath* path);

BUSTER_GLOBAL_LOCAL bool ttf_append_simple_glyph_path(Arena* arena, const TTF_FontInformation* information, TTF_GlyphRange range, s32 contour_count,
                                                      TTF_Transform transform, f32 scale_x, f32 scale_y, s32 x0, s32 y0, TTF_RasterPath* path)
{
    ByteSlice data = information->data;
    if (contour_count <= 0)
    {
        return true;
    }

    u64 contour_count_u64 = (u64)(u32)contour_count;
    if (!ttf_range_is_valid(data, range.offset + 10, contour_count_u64 * 2u + 2u))
    {
        return false;
    }

    u16 last_end_point = ttf_u16(data, range.offset + 10u + (contour_count_u64 - 1u) * 2u);
    u32 point_count = (u32)last_end_point + 1u;
    if (point_count == 0 || point_count > 65535u)
    {
        return false;
    }

    u16 instruction_length = ttf_u16(data, range.offset + 10u + contour_count_u64 * 2u);
    u64 point_data = range.offset + 10u + contour_count_u64 * 2u + 2u + (u64)instruction_length;
    if (!ttf_range_is_valid(data, point_data, 0))
    {
        return false;
    }

    TTF_Point* points = arena_allocate(arena, TTF_Point, point_count);
    u8* flags = arena_allocate(arena, u8, point_count);

    u64 cursor = point_data;
    for (u32 point = 0; point < point_count;)
    {
        if (!ttf_range_is_valid(data, cursor, 1))
        {
            return false;
        }
        u8 flag = ttf_u8(data, cursor);
        cursor += 1;
        u32 repeat_count = 1;
        if ((flag & 8u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 1))
            {
                return false;
            }
            repeat_count += (u32)ttf_u8(data, cursor);
            cursor += 1;
        }
        for (u32 repeat = 0; repeat < repeat_count && point < point_count; repeat += 1)
        {
            flags[point] = flag;
            points[point].on_curve = (flag & 1u) != 0;
            point += 1;
        }
    }

    s32* xs = arena_allocate(arena, s32, point_count);
    s32* ys = arena_allocate(arena, s32, point_count);
    s32 x = 0;
    for (u32 point = 0; point < point_count; point += 1)
    {
        u8 flag = flags[point];
        s32 dx = 0;
        if ((flag & 2u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 1))
            {
                return false;
            }
            dx = (s32)ttf_u8(data, cursor);
            cursor += 1;
            if ((flag & 16u) == 0)
            {
                dx = -dx;
            }
        }
        else if ((flag & 16u) == 0)
        {
            if (!ttf_range_is_valid(data, cursor, 2))
            {
                return false;
            }
            dx = (s32)ttf_s16(data, cursor);
            cursor += 2;
        }
        x += dx;
        xs[point] = x;
    }
    s32 y = 0;
    for (u32 point = 0; point < point_count; point += 1)
    {
        u8 flag = flags[point];
        s32 dy = 0;
        if ((flag & 4u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 1))
            {
                return false;
            }
            dy = (s32)ttf_u8(data, cursor);
            cursor += 1;
            if ((flag & 32u) == 0)
            {
                dy = -dy;
            }
        }
        else if ((flag & 32u) == 0)
        {
            if (!ttf_range_is_valid(data, cursor, 2))
            {
                return false;
            }
            dy = (s32)ttf_s16(data, cursor);
            cursor += 2;
        }
        y += dy;
        ys[point] = y;
    }
    for (u32 point = 0; point < point_count; point += 1)
    {
        TTF_Point transformed = ttf_transform_point(transform, xs[point], ys[point]);
        points[point].x = transformed.x;
        points[point].y = transformed.y;
    }

    u32 contour_start = 0;
    for (u32 contour = 0; contour < (u32)contour_count; contour += 1)
    {
        u32 contour_end = (u32)ttf_u16(data, range.offset + 10u + (u64)contour * 2u);
        if (contour_end < contour_start || contour_end >= point_count)
        {
            return false;
        }

        TTF_Point first = points[contour_start];
        TTF_Point last = points[contour_end];
        TTF_Point start_point = first.on_curve ? first : (last.on_curve ? last : ttf_midpoint(last, first));
        TTF_Point current = start_point;
        ttf_raster_path_add_point(path, ttf_pixel_point(start_point, scale_x, scale_y, x0, y0));

        u32 point = first.on_curve ? contour_start + 1u : contour_start;
        while (point <= contour_end)
        {
            TTF_Point p = points[point];
            if (p.on_curve)
            {
                ttf_append_line(path, p, scale_x, scale_y, x0, y0);
                current = p;
                point += 1;
            }
            else
            {
                TTF_Point next = point == contour_end ? first : points[point + 1u];
                if (next.on_curve)
                {
                    ttf_append_quadratic(path, current, p, next, scale_x, scale_y, x0, y0);
                    current = next;
                    point += 2u;
                }
                else
                {
                    TTF_Point mid = ttf_midpoint(p, next);
                    ttf_append_quadratic(path, current, p, mid, scale_x, scale_y, x0, y0);
                    current = mid;
                    point += 1u;
                }
            }
        }

        if (!ttf_points_equal(current, start_point))
        {
            ttf_append_line(path, start_point, scale_x, scale_y, x0, y0);
        }
        ttf_raster_path_end_contour(path);
        contour_start = contour_end + 1u;
    }

    return !path->overflowed;
}

BUSTER_GLOBAL_LOCAL bool ttf_append_compound_glyph_path(Arena* arena, const TTF_FontInformation* information, TTF_GlyphRange range, TTF_Transform transform,
                                                        f32 scale_x, f32 scale_y, s32 x0, s32 y0, u32 recursion_depth, TTF_RasterPath* path)
{
    ByteSlice data = information->data;
    u64 cursor = range.offset + 10u;
    bool more = true;
    while (more)
    {
        if (!ttf_range_is_valid(data, cursor, 4))
        {
            return false;
        }
        u16 flags = ttf_u16(data, cursor);
        u32 component_glyph = (u32)ttf_u16(data, cursor + 2u);
        cursor += 4u;

        s32 arg1 = 0;
        s32 arg2 = 0;
        if ((flags & 1u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 4))
            {
                return false;
            }
            arg1 = (s32)ttf_s16(data, cursor);
            arg2 = (s32)ttf_s16(data, cursor + 2u);
            cursor += 4u;
        }
        else
        {
            if (!ttf_range_is_valid(data, cursor, 2))
            {
                return false;
            }
            arg1 = ttf_s8_as_s32(data, cursor);
            arg2 = ttf_s8_as_s32(data, cursor + 1u);
            cursor += 2u;
        }

        f32 m00 = 1.0f;
        f32 m01 = 0.0f;
        f32 m10 = 0.0f;
        f32 m11 = 1.0f;
        if ((flags & 8u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 2))
            {
                return false;
            }
            f32 scale = (f32)ttf_s16(data, cursor) / 16384.0f;
            cursor += 2u;
            m00 = scale;
            m11 = scale;
        }
        else if ((flags & 64u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 4))
            {
                return false;
            }
            m00 = (f32)ttf_s16(data, cursor) / 16384.0f;
            m11 = (f32)ttf_s16(data, cursor + 2u) / 16384.0f;
            cursor += 4u;
        }
        else if ((flags & 128u) != 0)
        {
            if (!ttf_range_is_valid(data, cursor, 8))
            {
                return false;
            }
            m00 = (f32)ttf_s16(data, cursor) / 16384.0f;
            m01 = (f32)ttf_s16(data, cursor + 2u) / 16384.0f;
            m10 = (f32)ttf_s16(data, cursor + 4u) / 16384.0f;
            m11 = (f32)ttf_s16(data, cursor + 6u) / 16384.0f;
            cursor += 8u;
        }

        f32 dx = 0.0f;
        f32 dy = 0.0f;
        if ((flags & 2u) != 0)
        {
            dx = (f32)arg1;
            dy = (f32)arg2;
        }

        TTF_Transform component = {
            .m00 = transform.m00 * m00 + transform.m10 * m01,
            .m01 = transform.m01 * m00 + transform.m11 * m01,
            .m10 = transform.m00 * m10 + transform.m10 * m11,
            .m11 = transform.m01 * m10 + transform.m11 * m11,
            .dx = transform.m00 * dx + transform.m10 * dy + transform.dx,
            .dy = transform.m01 * dx + transform.m11 * dy + transform.dy,
        };
        if (!ttf_append_glyph_path(arena, information, component_glyph, component, scale_x, scale_y, x0, y0, recursion_depth + 1u, path))
        {
            return false;
        }

        more = (flags & 32u) != 0;
    }
    return !path->overflowed;
}

BUSTER_GLOBAL_LOCAL bool ttf_append_glyph_path(Arena* arena, const TTF_FontInformation* information, u32 glyph, TTF_Transform transform, f32 scale_x,
                                               f32 scale_y, s32 x0, s32 y0, u32 recursion_depth, TTF_RasterPath* path)
{
    if (recursion_depth > TTF_GLYPH_RECURSION_LIMIT)
    {
        return false;
    }

    TTF_GlyphRange range = truetype_glyph_range(information, glyph);
    if (range.length == 0)
    {
        return true;
    }
    if (!ttf_range_is_valid(information->data, range.offset, 10))
    {
        return false;
    }

    s32 contour_count = (s32)ttf_s16(information->data, range.offset);
    bool result = false;
    if (contour_count >= 0)
    {
        result = ttf_append_simple_glyph_path(arena, information, range, contour_count, transform, scale_x, scale_y, x0, y0, path);
    }
    else
    {
        result = ttf_append_compound_glyph_path(arena, information, range, transform, scale_x, scale_y, x0, y0, recursion_depth, path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL f32 ttf_edge_left(TTF_RasterPoint a, TTF_RasterPoint b, f32 x, f32 y)
{
    f32 result = (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ttf_path_contains_point(const TTF_RasterPath* path, f32 x, f32 y)
{
    s32 winding = 0;
    u32 contour_start = 0;
    for (u32 contour = 0; contour < path->contour_count; contour += 1)
    {
        u32 contour_end = path->contour_ends[contour];
        if (contour_end > contour_start + 1u)
        {
            for (u32 point = contour_start; point < contour_end; point += 1)
            {
                TTF_RasterPoint a = path->points[point];
                TTF_RasterPoint b = path->points[(point + 1u < contour_end) ? point + 1u : contour_start];
                if (a.y <= y)
                {
                    if (b.y > y && ttf_edge_left(a, b, x, y) > 0.0f)
                    {
                        winding += 1;
                    }
                }
                else if (b.y <= y && ttf_edge_left(a, b, x, y) < 0.0f)
                {
                    winding -= 1;
                }
            }
        }
        contour_start = contour_end;
    }
    bool result = winding != 0;
    return result;
}

TTF_Bitmap truetype_get_codepoint_bitmap(Arena* arena, const TTF_FontInformation* information, f32 scale_x, f32 scale_y, u32 codepoint)
{
    TTF_Bitmap result = {0};
    u32 glyph = truetype_glyph_index_from_codepoint(information, codepoint);
    TTF_GlyphRange range = truetype_glyph_range(information, glyph);
    if (range.length == 0 || !ttf_range_is_valid(information->data, range.offset, 10))
    {
        return result;
    }

    s32 x_min = (s32)ttf_s16(information->data, range.offset + 2u);
    s32 y_min = (s32)ttf_s16(information->data, range.offset + 4u);
    s32 x_max = (s32)ttf_s16(information->data, range.offset + 6u);
    s32 y_max = (s32)ttf_s16(information->data, range.offset + 8u);

    s32 x0 = (s32)floor_f32((f32)x_min * scale_x);
    s32 y0 = (s32)floor_f32(-(f32)y_max * scale_y);
    s32 x1 = (s32)ceil_f32((f32)x_max * scale_x);
    s32 y1 = (s32)ceil_f32(-(f32)y_min * scale_y);
    s32 width = x1 - x0;
    s32 height = y1 - y0;

    result.x_offset = x0;
    result.y_offset = y0;
    result.width = width > 0 ? width : 0;
    result.height = height > 0 ? height : 0;
    if (result.width == 0 || result.height == 0)
    {
        return result;
    }

    u32 max_points = (u32)information->max_points + (u32)information->max_composite_points;
    u32 max_contours = (u32)information->max_contours + (u32)information->max_composite_contours;
    if (max_points < 256u)
    {
        max_points = 256u;
    }
    if (max_contours < 64u)
    {
        max_contours = 64u;
    }
    u32 raster_point_capacity = max_points * (TTF_CURVE_SEGMENTS + 1u) + max_contours * 2u + 64u;
    TTF_RasterPath path = {
        .points = arena_allocate(arena, TTF_RasterPoint, raster_point_capacity),
        .contour_ends = arena_allocate(arena, u32, max_contours),
        .point_capacity = raster_point_capacity,
        .contour_capacity = max_contours,
    };

    TTF_Transform identity = {.m00 = 1.0f, .m11 = 1.0f};
    if (!ttf_append_glyph_path(arena, information, glyph, identity, scale_x, scale_y, x0, y0, 0, &path) || path.overflowed)
    {
        return result;
    }

    u64 pixel_count = (u64)(u32)result.width * (u64)(u32)result.height;
    result.pixels = arena_allocate(arena, u8, pixel_count);
    u32 sample_count = TTF_RASTER_SUBSAMPLES * TTF_RASTER_SUBSAMPLES;
    for (s32 py = 0; py < result.height; py += 1)
    {
        for (s32 px = 0; px < result.width; px += 1)
        {
            u32 covered = 0;
            for (u32 sy = 0; sy < TTF_RASTER_SUBSAMPLES; sy += 1)
            {
                for (u32 sx = 0; sx < TTF_RASTER_SUBSAMPLES; sx += 1)
                {
                    f32 sample_x = (f32)px + ((f32)sx + 0.5f) / (f32)TTF_RASTER_SUBSAMPLES;
                    f32 sample_y = (f32)py + ((f32)sy + 0.5f) / (f32)TTF_RASTER_SUBSAMPLES;
                    if (ttf_path_contains_point(&path, sample_x, sample_y))
                    {
                        covered += 1;
                    }
                }
            }
            u64 index = (u64)(u32)py * (u64)(u32)result.width + (u64)(u32)px;
            result.pixels[index] = (u8)((covered * 255u + sample_count / 2u) / sample_count);
        }
    }

    return result;
}
