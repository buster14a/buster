// Headless TrueType bitmap admission tests. truetype_tests uses a bounded,
// synthetic outlines, including original-point compound attachment, so every
// platform checks identical bytes.
// The parameter sweep also runs in CI's sanitizer and fuzz-enabled trees.
#include <buster/tests/truetype_test.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/truetype_internal.h>

BUSTER_GLOBAL_LOCAL void truetype_test_u16(u8* bytes, u32 offset, s32 value)
{
    u16 bits = (u16)value;
    bytes[offset] = (u8)(bits >> 8u);
    bytes[offset + 1u] = (u8)bits;
}

BUSTER_GLOBAL_LOCAL bool truetype_test_empty(TTF_Bitmap bitmap)
{
    bool result = !bitmap.pixels && !bitmap.width && !bitmap.height && !bitmap.x_offset && !bitmap.y_offset;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 truetype_test_random(u32* state)
{
    u32 result = *state;
    result ^= result << 13u;
    result ^= result >> 17u;
    result ^= result << 5u;
    *state = result;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 truetype_test_bitmap_hash(TTF_Bitmap bitmap)
{
    u64 result = 14695981039346656037ull;
    u64 pixel_count = (u64)(u32)bitmap.width * (u64)(u32)bitmap.height;
    for (u64 pixel = 0; pixel < pixel_count; pixel += 1)
    {
        result = (result ^ bitmap.pixels[pixel]) * 1099511628211ull;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void truetype_test_u32(u8* bytes, u32 offset, u32 value)
{
    bytes[offset] = (u8)(value >> 24u);
    bytes[offset + 1u] = (u8)(value >> 16u);
    bytes[offset + 2u] = (u8)(value >> 8u);
    bytes[offset + 3u] = (u8)value;
}

typedef struct TruetypeSourceTestPoint TruetypeSourceTestPoint;
struct TruetypeSourceTestPoint
{
    s16 x;
    s16 y;
    u8 flag;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL u32 truetype_test_simple_outline(u8* bytes, u32 offset, const TruetypeSourceTestPoint* points, u32 point_count, u32 padding)
{
    truetype_test_u16(bytes, offset, 1);
    truetype_test_u16(bytes, offset + 6u, 700);
    truetype_test_u16(bytes, offset + 8u, 700);
    truetype_test_u16(bytes, offset + 10u, (s32)(padding + point_count - 1u));
    u32 cursor = offset + 14u;
    // Repeated stationary on-curve points keep high unsigned indices compact.
    for (u32 remaining = padding; remaining != 0;)
    {
        u32 count = BUSTER_MIN(remaining, 256u);
        bytes[cursor++] = 57;
        bytes[cursor++] = (u8)(count - 1u);
        remaining -= count;
    }
    for (u32 point = 0; point < point_count; point += 1)
    {
        bytes[cursor++] = points[point].flag;
    }
    for (u32 axis = 0; axis < 2; axis += 1)
    {
        s32 previous = 0;
        for (u32 point = 0; point < point_count; point += 1)
        {
            s32 coordinate = axis == 0 ? points[point].x : points[point].y;
            truetype_test_u16(bytes, cursor, coordinate - previous);
            cursor += 2u;
            previous = coordinate;
        }
    }
    return cursor;
}

BUSTER_GLOBAL_LOCAL u32 truetype_test_component(u8* bytes, u32 cursor, u16 flags, u16 glyph, s32 arg1, s32 arg2, const s16* matrix)
{
    truetype_test_u16(bytes, cursor, flags);
    truetype_test_u16(bytes, cursor + 2u, glyph);
    cursor += 4u;
    if ((flags & 1u) != 0)
    {
        truetype_test_u16(bytes, cursor, arg1);
        truetype_test_u16(bytes, cursor + 2u, arg2);
        cursor += 4u;
    }
    else
    {
        bytes[cursor++] = (u8)arg1;
        bytes[cursor++] = (u8)arg2;
    }
    u32 matrix_count = (flags & 8u) != 0 ? 1u : ((flags & 64u) != 0 ? 2u : ((flags & 128u) != 0 ? 4u : 0u));
    for (u32 value = 0; value < matrix_count; value += 1)
    {
        truetype_test_u16(bytes, cursor, matrix[value]);
        cursor += 2u;
    }
    return cursor;
}

BUSTER_GLOBAL_LOCAL void truetype_test_compound_header(u8* bytes, u32 offset)
{
    truetype_test_u16(bytes, offset, -1);
    truetype_test_u16(bytes, offset + 6u, 700);
    truetype_test_u16(bytes, offset + 8u, 700);
}

BUSTER_GLOBAL_LOCAL UnitTestResult truetype_test_compound_attachments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    // Glyphs 0/1 are matched-point/independent XY versions, 2/3 are leaves,
    // and 4/5 are nested matched-point/independent XY versions. Cmap is one
    // format-12 group, mapping codepoints 0..5 directly to those glyphs.
    for (u32 test_case = 0; test_case < 8; test_case += 1)
    {
        u8 bytes[4096] = {0};
        u32 offsets[7];
        u32 cursor = 64;
        TruetypeSourceTestPoint base[] = {{200, 0, 1, {0}}, {400, 0, 1, {0}}, {400, 400, 1, {0}}, {200, 400, 1, {0}}};
        TruetypeSourceTestPoint triangle[] = {{0, 0, 1, {0}}, {100, 0, 1, {0}}, {50, 100, 1, {0}}};
        TruetypeSourceTestPoint shifted_triangle[] = {{20, 20, 1, {0}}, {120, 20, 1, {0}}, {70, 120, 1, {0}}};
        TruetypeSourceTestPoint curve[] = {{20, 20, 1, {0}}, {40, 100, 0, {0}}, {60, 100, 0, {0}}, {120, 20, 1, {0}}};
        u32 padding = test_case == 1 ? 128u : (test_case == 2 ? 32768u : 0u);
        u16 attachment_flags = test_case == 1 ? 8u : (test_case == 2 ? 129u : 1u);
        s16 matrix[] = {8192, 4096, -4096, 12288};
        s16 scale[] = {8192};
        s16 nested_scale[] = {8192, 12288};
        s16 singular[] = {8192, 4096, 8192, 4096};
        s16 outer_matrix[] = {16384, 4096, 8192, 16384};
        s32 parent_point = (s32)padding + 2;
        s32 child_point = (s32)padding;
        s32 dx = 400;
        s32 dy = 400;
        u16 attached_child = 3;
        u16 xy_child = 3;
        u16 base_glyph = 2;
        u16 base_flags = 35;
        s32 base_dx = 0;
        const s16* child_matrix = 0;
        const s16* base_matrix = 0;
        if (test_case == 1)
        {
            dx = 390;
            dy = 390;
            child_matrix = scale;
        }
        else if (test_case == 2)
        {
            dx = 395;
            dy = 380;
            child_matrix = matrix;
        }
        else if (test_case == 3)
        {
            child_point = 2;
            dx = 340;
            dy = 300;
        }
        else if (test_case == 4)
        {
            attached_child = 4;
            xy_child = 5;
            attachment_flags = 65;
            child_matrix = nested_scale;
            child_point = 4;
            dx = 360;
            dy = 295;
        }
        else if (test_case == 7)
        {
            attached_child = 4;
            xy_child = 5;
            attachment_flags = 129;
            child_matrix = outer_matrix;
            child_point = 4;
            dx = 250;
            dy = 240;
        }
        else if (test_case >= 5)
        {
            base_glyph = 4;
            base_dx = 200;
            parent_point = test_case == 5 ? 5 : 4;
            dx = test_case == 5 ? 230 : 310;
            dy = test_case == 5 ? 240 : 55;
            if (test_case == 6)
            {
                base_flags |= 128u;
                base_matrix = singular;
            }
        }
        for (u32 glyph = 0; glyph < 2; glyph += 1)
        {
            offsets[glyph] = cursor - 64u;
            truetype_test_compound_header(bytes, cursor);
            cursor += 10u;
            if (test_case == 0)
            {
                // The original issue's rectangle/triangle pair has tight bounds.
                truetype_test_u16(bytes, cursor - 8u, 200);
                truetype_test_u16(bytes, cursor - 4u, 500);
                truetype_test_u16(bytes, cursor - 2u, 500);
            }
            u16 first_glyph = glyph == 1 && (test_case == 5 || test_case == 6) ? 5 : base_glyph;
            cursor = truetype_test_component(bytes, cursor, base_flags, first_glyph, base_dx, 0, base_matrix);
            u16 flags = glyph == 0 ? attachment_flags : (u16)(attachment_flags | 3u);
            cursor = truetype_test_component(bytes, cursor, flags, glyph == 0 ? attached_child : xy_child,
                                             glyph == 0 ? parent_point : dx, glyph == 0 ? child_point : dy, child_matrix);
        }
        offsets[2] = cursor - 64u;
        cursor = truetype_test_simple_outline(bytes, cursor, base, BUSTER_ARRAY_LENGTH(base), padding);
        offsets[3] = cursor - 64u;
        const TruetypeSourceTestPoint* child_points = padding ? shifted_triangle : (test_case == 3 ? curve : triangle);
        u32 child_count = test_case == 3 ? BUSTER_ARRAY_LENGTH(curve) : BUSTER_ARRAY_LENGTH(triangle);
        cursor = truetype_test_simple_outline(bytes, cursor, child_points, child_count, padding);
        for (u32 glyph = 4; glyph < 6; glyph += 1)
        {
            offsets[glyph] = cursor - 64u;
            truetype_test_compound_header(bytes, cursor);
            cursor += 10u;
            cursor = truetype_test_component(bytes, cursor, 35, 3, 30, 40, 0);
            u16 flags = glyph == 4 ? 1 : 3;
            s32 nested_dx = -20;
            s32 nested_dy = 140;
            const s16* nested_matrix = 0;
            if (test_case == 7)
            {
                // Leaf and ancestor matrices do not commute; match after both.
                flags |= 128u;
                nested_matrix = matrix;
                nested_dx = 30;
                nested_dy = 115;
            }
            cursor = truetype_test_component(bytes, cursor, flags, 3, glyph == 4 ? 2 : nested_dx, glyph == 4 ? 1 : nested_dy, nested_matrix);
        }
        offsets[6] = cursor - 64u;
        for (u32 glyph = 0; glyph < BUSTER_ARRAY_LENGTH(offsets); glyph += 1)
        {
            truetype_test_u32(bytes, glyph * 4u, offsets[glyph]);
        }
        truetype_test_u32(bytes, 44, 1);
        truetype_test_u32(bytes, 52, 5);
        TTF_FontInformation font = {.data = {.pointer = bytes, .length = cursor}, .glyf = 64, .cmap_subtable = 32,
                                    .cmap_format = 12, .num_glyphs = 6, .index_to_loc_format = 1};
        f32 bitmap_scale = test_case == 2 ? 0.01f : 0.1f;
        TTF_Bitmap attached = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
        u8 reference[4900];
        bool attached_valid = attached.pixels && attached.width > 0 && attached.height > 0 && attached.width <= 70 && attached.height <= 70;
        if (BUSTER_REQUIRE(arguments, attached_valid))
        {
            if (test_case == 0)
            {
                BUSTER_TEST(arguments, attached.width == 30 && attached.height == 50 && attached.x_offset == 20 && attached.y_offset == -50);
            }
            memcpy(reference, attached.pixels, (size_t)attached.width * (size_t)attached.height);
            arena_set_position(arena, position);
            TTF_Bitmap xy = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 1);
            bool same_bounds = xy.pixels && xy.width == attached.width && xy.height == attached.height &&
                               xy.x_offset == attached.x_offset && xy.y_offset == attached.y_offset;
            if (BUSTER_REQUIRE(arguments, same_bounds))
            {
                BUSTER_TEST(arguments, memcmp(reference, xy.pixels, (size_t)xy.width * (size_t)xy.height) == 0);
                u32 coverage = 0;
                for (u32 pixel = 0; pixel < (u32)xy.width * (u32)xy.height; pixel += 1)
                {
                    coverage += xy.pixels[pixel];
                }
                BUSTER_TEST(arguments, coverage != 0);
                if (test_case == 0)
                {
                    BUSTER_TEST(arguments, coverage == 216750);
                }
            }
        }
        arena_set_position(arena, position);
        if (test_case == 0)
        {
            // Parent index == preceding point count, child index == child
            // count, first-component attachment, empty child and a cycle all
            // fail without retaining any partial arena allocation.
            u32 second = 64u + 10u + 8u;
            s32 invalid_indices[][2] = {{4, 0}, {2, 3}, {65535, 0}, {2, 65535}};
            for (u32 invalid = 0; invalid < BUSTER_ARRAY_LENGTH(invalid_indices); invalid += 1)
            {
                truetype_test_u16(bytes, second + 4u, invalid_indices[invalid][0]);
                truetype_test_u16(bytes, second + 6u, invalid_indices[invalid][1]);
                TTF_Bitmap rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
                BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            }
            truetype_test_u16(bytes, second + 4u, 2);
            truetype_test_u16(bytes, second + 6u, 0);
            truetype_test_u16(bytes, 74, 33);
            TTF_Bitmap rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u16(bytes, 74, 35);
            truetype_test_u16(bytes, 64u + offsets[3], 0);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u16(bytes, 64u + offsets[3], 1);
            truetype_test_u16(bytes, second + 2u, 6);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u16(bytes, second, 257);
            truetype_test_u16(bytes, second + 2u, 3);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            // Extend only this glyph by two bytes for a zero-length instruction
            // block; a one-byte payload must not borrow the following glyph.
            truetype_test_u32(bytes, 4, offsets[1] + 2u);
            truetype_test_u16(bytes, 64u + offsets[1], 0);
            TTF_Bitmap instructed = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, instructed.pixels && instructed.width == 30 && instructed.height == 50);
            arena_set_position(arena, position);
            truetype_test_u16(bytes, 64u + offsets[1], 1);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u32(bytes, 4, offsets[1]);
            truetype_test_u16(bytes, 64u + offsets[1], -1);
            truetype_test_u16(bytes, second, 1);
            truetype_test_u16(bytes, second + 2u, 0);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
        }
    }
    u8 depth_bytes[512] = {0};
    u32 depth_cursor = 80;
    for (u32 glyph = 0; glyph < 10; glyph += 1)
    {
        truetype_test_u32(depth_bytes, glyph * 4u, depth_cursor - 80u);
        if (glyph < 9)
        {
            truetype_test_compound_header(depth_bytes, depth_cursor);
            depth_cursor = truetype_test_component(depth_bytes, depth_cursor + 10u, 3, (u16)(glyph + 1u), 0, 0, 0);
        }
        else
        {
            TruetypeSourceTestPoint rectangle[] = {{0, 0, 1, {0}}, {10, 0, 1, {0}}, {10, 10, 1, {0}}, {0, 10, 1, {0}}};
            depth_cursor = truetype_test_simple_outline(depth_bytes, depth_cursor, rectangle, BUSTER_ARRAY_LENGTH(rectangle), 0);
        }
    }
    truetype_test_u32(depth_bytes, 40, depth_cursor - 80u);
    truetype_test_u32(depth_bytes, 60, 1);
    truetype_test_u32(depth_bytes, 68, 9);
    TTF_FontInformation depth_font = {.data = {.pointer = depth_bytes, .length = depth_cursor}, .glyf = 80, .cmap_subtable = 48,
                                      .cmap_format = 12, .num_glyphs = 10, .index_to_loc_format = 1};
    TTF_Bitmap depth_bitmap = truetype_get_codepoint_bitmap(arena, &depth_font, 0.1f, 0.1f, 1);
    BUSTER_TEST(arguments, depth_bitmap.pixels && depth_bitmap.width == 70 && depth_bitmap.height == 70);
    arena_set_position(arena, position);
    depth_bitmap = truetype_get_codepoint_bitmap(arena, &depth_font, 0.1f, 0.1f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(depth_bitmap) && arena->position == position);
    return result;
}

UnitTestResult truetype_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = truetype_test_compound_attachments(arguments);
    // Long loca entries [0,34], then a rectangle with bounds (-2,-3)-(6,5).
    // Four on-curve points use signed 16-bit x/y deltas without instructions.
    u8 bytes[] = {
        0, 0, 0, 0, 0, 0, 0, 34,
        0, 1, 255, 254, 255, 253, 0, 6, 0, 5,
        0, 3, 0, 0, 1, 1, 1, 1,
        255, 254, 0, 8, 0, 0, 255, 248,
        255, 253, 0, 0, 0, 8, 0, 0,
    };
    TTF_FontInformation font = {
        .data = {.pointer = bytes, .length = sizeof(bytes)},
        .glyf = 8,
        .num_glyphs = 1,
        .index_to_loc_format = 1,
        .max_points = 4,
        .max_contours = 1,
    };
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    TTF_Bitmap bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
    BUSTER_TEST(arguments, bitmap.pixels && bitmap.width == 8 && bitmap.height == 8 && bitmap.x_offset == -2 && bitmap.y_offset == -5);
    bool solid = bitmap.pixels != 0;
    for (u32 pixel = 0; pixel < 64 && solid; pixel += 1)
    {
        solid = bitmap.pixels[pixel] == 255;
    }
    BUSTER_TEST(arguments, solid);
    arena_set_position(arena, position);

    TTF_RasterTestPoint curve_from = {.x = 0.0f, .y = 0.0f};
    TTF_RasterTestPoint curve_control = {.x = 50.0f, .y = 100.0f};
    TTF_RasterTestPoint curve_to = {.x = 100.0f, .y = 0.0f};
    TTF_QuadraticTestResult flat_curve = truetype_flatten_quadratic_for_test(curve_from, (TTF_RasterTestPoint){.x = 50.0f, .y = 0.01f}, curve_to, 1.0f, 1.0f);
    BUSTER_TEST(arguments, flat_curve.segment_count == 1 && flat_curve.maximum_error <= 0.25f && !flat_curve.subdivision_limit_reached);

    f32 curve_scales[] = {0.01f, 0.1f, 1.0f};
    u32 curve_segment_counts[] = {2, 8, 16};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(curve_scales); index += 1)
    {
        TTF_QuadraticTestResult curve =
            truetype_flatten_quadratic_for_test(curve_from, curve_control, curve_to, curve_scales[index], curve_scales[index]);
        BUSTER_TEST(arguments, curve.segment_count == curve_segment_counts[index] && curve.maximum_error <= 0.25f && !curve.subdivision_limit_reached);
    }

    TTF_QuadraticTestResult extreme_curve =
        truetype_flatten_quadratic_for_test(curve_from, (TTF_RasterTestPoint){.x = 50.0f, .y = 10000000000.0f}, curve_to, 1.0f, 1.0f);
    BUSTER_TEST(arguments, extreme_curve.segment_count == 1024 && extreme_curve.maximum_error > 0.25f && extreme_curve.subdivision_limit_reached);

    // One quadratic arch and three lines form a deterministic curved glyph.
    // These three pixel-space scales are visual goldens for small, normal and
    // large rendering, including a curve that exceeds the old 12 segments.
    u8 curve_bytes[] = {
        0, 0, 0, 0, 0, 0, 0, 39,
        0, 1, 0, 0, 0, 0, 0, 64, 0, 64,
        0, 4, 0, 0,
        1, 0, 1, 1, 1,
        0, 0, 0, 32, 0, 32, 0, 0, 255, 192,
        0, 0, 0, 64, 255, 192, 0, 64, 0, 0,
    };
    TTF_FontInformation curve_font = {
        .data = {.pointer = curve_bytes, .length = sizeof(curve_bytes)},
        .glyf = 8,
        .num_glyphs = 1,
        .index_to_loc_format = 1,
        .max_points = 5,
        .max_contours = 1,
    };
    f32 bitmap_scales[] = {0.125f, 0.5f, 2.0f};
    u32 bitmap_dimensions[] = {8, 32, 128};
    u64 bitmap_hashes[] = {5808018917995742617ull, 17460563367413290325ull, 11190615675908677403ull};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bitmap_scales); index += 1)
    {
        bitmap = truetype_get_codepoint_bitmap(arena, &curve_font, bitmap_scales[index], bitmap_scales[index], 0);
        u64 hash = bitmap.pixels ? truetype_test_bitmap_hash(bitmap) : 0;
        BUSTER_TEST(arguments,
                    bitmap.pixels && bitmap.width == (s32)bitmap_dimensions[index] && bitmap.height == (s32)bitmap_dimensions[index] && hash == bitmap_hashes[index]);
        arena_set_position(arena, position);
    }

    bitmap = truetype_get_codepoint_bitmap(arena, &font, 0.5f, 0.25f, 0);
    BUSTER_TEST(arguments, bitmap.pixels && bitmap.width == 4 && bitmap.height == 3 && bitmap.x_offset == -1 && bitmap.y_offset == -2);
    arena_set_position(arena, position);

    u32 rejected_scale_bits[] = {
        0, 0x80000000u, 0xbf800000u, 0x7fc00000u, 0xffc00000u,
        0x7f800000u, 0xff800000u, 0x7f7fffffu, 0xff7fffffu, 0x47800001u,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejected_scale_bits); index += 1)
    {
        f32 scale;
        memcpy(&scale, &rejected_scale_bits[index], sizeof(scale));
        bitmap = truetype_get_codepoint_bitmap(arena, &font, scale, 1.0f, 0);
        BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
        bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, scale, 0);
        BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
    }
    bitmap = truetype_get_codepoint_bitmap(arena, &font, -1.0f, -1.0f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);

    typedef struct TruetypeBoundsCase TruetypeBoundsCase;
    struct TruetypeBoundsCase
    {
        s32 x_min;
        s32 y_min;
        s32 x_max;
        s32 y_max;
        f32 scale_x;
        f32 scale_y;
    };
    TruetypeBoundsCase rejected_bounds[] = {
        {0, 0, 4097, 1, 1.0f, 1.0f},
        {0, 0, 1, 4097, 1.0f, 1.0f},
        {0, 0, 1024, 1025, 1.0f, 1.0f},
        {0, 0, 4096, 4096, 1.0f, 1.0f},
        {-32768, -32768, 32767, 32767, 1.0f, 1.0f},
        // Every x endpoint fits s32; their difference does not.
        {-32768, 0, 32767, 1, BUSTER_TTF_MAX_SCALE, 1.0f},
        // Negating y_min produces exactly 2^31 at the conversion boundary.
        {0, -32768, 1, -32767, 1.0f, BUSTER_TTF_MAX_SCALE},
        {6, -3, -2, 5, 1.0f, 1.0f},
        {-2, 5, 6, -3, 1.0f, 1.0f},
        {-2, -3, -2, 5, 1.0f, 1.0f},
        {-2, -3, 6, -3, 1.0f, 1.0f},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejected_bounds); index += 1)
    {
        TruetypeBoundsCase bounds = rejected_bounds[index];
        truetype_test_u16(bytes, 10, bounds.x_min);
        truetype_test_u16(bytes, 12, bounds.y_min);
        truetype_test_u16(bytes, 14, bounds.x_max);
        truetype_test_u16(bytes, 16, bounds.y_max);
        bitmap = truetype_get_codepoint_bitmap(arena, &font, bounds.scale_x, bounds.scale_y, 0);
        BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
    }

    // Inclusive dimension/pixel limits must still allow useful output.
    u32 accepted_dimensions[][2] = {{4096, 1}, {1, 4096}, {1024, 1024}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(accepted_dimensions); index += 1)
    {
        u32 width = accepted_dimensions[index][0];
        u32 height = accepted_dimensions[index][1];
        truetype_test_u16(bytes, 10, 0);
        truetype_test_u16(bytes, 12, 0);
        truetype_test_u16(bytes, 14, (s32)width);
        truetype_test_u16(bytes, 16, (s32)height);
        bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
        BUSTER_TEST(arguments, bitmap.pixels && bitmap.width == (s32)width && bitmap.height == (s32)height);
        arena_set_position(arena, position);
    }

    truetype_test_u16(bytes, 10, -2);
    truetype_test_u16(bytes, 12, -3);
    truetype_test_u16(bytes, 14, 6);
    truetype_test_u16(bytes, 16, 5);
    // Truncated headers must not borrow bytes from the next glyph. Reversed,
    // empty, and out-of-file loca ranges must not allocate either.
    u8 range_ends[] = {0, 1, 8, 9, 35, 255};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(range_ends); index += 1)
    {
        bytes[7] = range_ends[index];
        bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
        BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
    }
    bytes[3] = 35;
    bytes[7] = 34;
    bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
    bytes[3] = 0;
    font.data.length = 7;
    bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);
    font.data.length = sizeof(bytes);
    truetype_test_u16(bytes, 8, 0);
    bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f, 1.0f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(bitmap));
    arena_set_position(arena, position);
    truetype_test_u16(bytes, 8, 1);

    truetype_test_u16(bytes, 10, -32768);
    truetype_test_u16(bytes, 12, -32768);
    truetype_test_u16(bytes, 14, 32767);
    truetype_test_u16(bytes, 16, 32767);
    bitmap = truetype_get_codepoint_bitmap(arena, &font, 1.0f / 32768.0f, 1.0f / 32768.0f, 0);
    BUSTER_TEST(arguments, bitmap.pixels && bitmap.width == 2 && bitmap.height == 2 && bitmap.x_offset == -1 && bitmap.y_offset == -1);
    arena_set_position(arena, position);

    // 1px * 4 samples uses three edge-search steps on each of 16384 rows.
    u32 edge_limit = BUSTER_TTF_MAX_RASTER_EDGE_STEPS / (4096u * 4u) / 3u;
    BUSTER_TEST(arguments, truetype_bitmap_work_is_valid_for_test(1, 4096, edge_limit));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(1, 4096, edge_limit + 1u));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(4096, 256, UINT32_MAX));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(1, 1, BUSTER_TTF_MAX_RASTER_POINTS + 1u));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(0, 1, 1));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(0, 1, 0));
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(1, 0, 1));

    // Six repeated-flag runs encode 1536 stationary on-curve points. Even
    // though the declared bitmap fits, its conservative edge budget does not.
    u8 busy_bytes[] = {
        0, 0, 0, 0, 0, 0, 0, 26,
        0, 1, 0, 0, 0, 0, 0, 1, 16, 0,
        5, 255, 0, 0,
        57, 255, 57, 255, 57, 255, 57, 255, 57, 255, 57, 255,
    };
    TTF_FontInformation busy_font = font;
    busy_font.data = (ByteSlice){.pointer = busy_bytes, .length = sizeof(busy_bytes)};
    busy_font.max_points = 1536;
    bitmap = truetype_get_codepoint_bitmap(arena, &busy_font, 1.0f, 1.0f, 0);
    BUSTER_TEST(arguments, truetype_test_empty(bitmap) && arena->position == position);

    u32 random = 0x5c782a91u;
    bool sweep_valid = true;
    for (u32 iteration = 0; iteration < 512 && sweep_valid; iteration += 1)
    {
        u32 scale_bits = truetype_test_random(&random);
        f32 scale_x;
        memcpy(&scale_x, &scale_bits, sizeof(scale_x));
        scale_bits = truetype_test_random(&random);
        f32 scale_y;
        memcpy(&scale_y, &scale_bits, sizeof(scale_y));
        for (u32 offset = 10; offset < 18; offset += 2)
        {
            s32 bound = (s32)(truetype_test_random(&random) & 0xffffu) - 32768;
            truetype_test_u16(bytes, offset, bound);
        }
        bitmap = truetype_get_codepoint_bitmap(arena, &font, scale_x, scale_y, 0);
        if (bitmap.pixels)
        {
            sweep_valid = bitmap.width > 0 && bitmap.width <= (s32)BUSTER_TTF_MAX_BITMAP_WIDTH &&
                          bitmap.height > 0 && bitmap.height <= (s32)BUSTER_TTF_MAX_BITMAP_HEIGHT &&
                          (u64)(u32)bitmap.width * (u64)(u32)bitmap.height <= BUSTER_TTF_MAX_BITMAP_PIXELS;
        }
        else
        {
            sweep_valid = truetype_test_empty(bitmap) && arena->position == position;
        }
        arena_set_position(arena, position);
    }
    BUSTER_TEST(arguments, sweep_valid);
    return result;
}
#endif
