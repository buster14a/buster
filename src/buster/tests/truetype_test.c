// Headless TrueType bitmap admission tests. truetype_tests uses a bounded,
// synthetic outlines, including original-point and phantom-point compound attachment, so every
// platform checks identical bytes.
// The parameter sweep also runs in CI's sanitizer and fuzz-enabled trees.
#include <buster/tests/truetype_test.h>

#if BUSTER_INCLUDE_TESTS
// truetype_test_kerning covers subtable composition using synthetic cmap/kern bytes.
#include <buster/lib/truetype_internal.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>

BUSTER_GLOBAL_LOCAL void truetype_test_u16(u8* bytes, u32 offset, s32 value)
{
    u16 bits = (u16)value;
    bytes[offset] = (u8)(bits >> 8u);
    bytes[offset + 1u] = (u8)bits;
}

BUSTER_GLOBAL_LOCAL UnitTestResult truetype_test_kerning(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Format-4 cmap maps A/B to glyphs 1/2. The version-0 kern table at 32
    // holds up to three 20-byte format-0 subtables, each with one A/B pair.
    u8 bytes[96] = {
        0, 4, 0, 32, 0, 0, 0, 4, 0, 4, 0, 1, 0, 0,
        0, 66, 255, 255, 0, 0, 0, 65, 255, 255,
        255, 192, 0, 1, 0, 0, 0, 0,
    };
    u32 table_count_offset = 34;
    u32 first_subtable = 36;
    u32 second_subtable = 56;
    s32 values[] = {-20, -30, 5};
    truetype_test_u16(bytes, table_count_offset, 2);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        u32 subtable = first_subtable + index * 20u;
        truetype_test_u16(bytes, subtable + 2u, 20); // length
        truetype_test_u16(bytes, subtable + 4u, 1); // horizontal coverage
        truetype_test_u16(bytes, subtable + 6u, 1); // pair count
        truetype_test_u16(bytes, subtable + 8u, 6); // search range
        truetype_test_u16(bytes, subtable + 14u, 1); // left glyph
        truetype_test_u16(bytes, subtable + 16u, 2); // right glyph
        truetype_test_u16(bytes, subtable + 18u, values[index]);
    }
    TTF_FontInformation font = {
        .data = {.pointer = bytes, .length = sizeof(bytes)},
        .kern = 32,
        .cmap_format = 4,
        .num_glyphs = 3,
    };
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -50);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'A') == 0);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'B', 'A') == 0);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'B', 'B') == 0);

    truetype_test_u16(bytes, first_subtable + 18u, -30);
    truetype_test_u16(bytes, second_subtable + 18u, -20);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -50);
    truetype_test_u16(bytes, first_subtable + 18u, -20);
    truetype_test_u16(bytes, second_subtable + 18u, -30);

    s32 signed_values[] = {32767, -32768};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(signed_values); index += 1)
    {
        truetype_test_u16(bytes, first_subtable + 18u, signed_values[index]);
        truetype_test_u16(bytes, second_subtable + 18u, signed_values[index]);
        BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == signed_values[index] * 2);
    }
    truetype_test_u16(bytes, first_subtable + 18u, 40);
    truetype_test_u16(bytes, second_subtable + 18u, -30);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == 10);
    truetype_test_u16(bytes, first_subtable + 18u, -20);

    truetype_test_u16(bytes, second_subtable + 4u, 9); // later override
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -30);
    truetype_test_u16(bytes, table_count_offset, 3);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -25);
    truetype_test_u16(bytes, table_count_offset, 2);
    truetype_test_u16(bytes, second_subtable + 18u, 0);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == 0);
    truetype_test_u16(bytes, second_subtable + 18u, -30);

    truetype_test_u16(bytes, second_subtable + 14u, 2); // override has no A/B
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -20);
    truetype_test_u16(bytes, second_subtable + 14u, 1);
    truetype_test_u16(bytes, second_subtable + 4u, 1);
    truetype_test_u16(bytes, first_subtable + 14u, 2); // only later table matches
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -30);
    truetype_test_u16(bytes, first_subtable + 14u, 1);

    // Vertical, minimum, cross-stream (including overrides), unsupported formats.
    s32 ignored_coverages[] = {0, 3, 5, 7, 8, 11, 13, 15, 0x0101, 0x0201};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(ignored_coverages); index += 1)
    {
        truetype_test_u16(bytes, second_subtable + 4u, ignored_coverages[index]);
        BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -20);
    }
    truetype_test_u16(bytes, first_subtable + 4u, 3);
    truetype_test_u16(bytes, second_subtable + 4u, 5);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == 0);
    truetype_test_u16(bytes, first_subtable + 4u, 1);
    truetype_test_u16(bytes, second_subtable + 4u, 1);

    // Reject pair counts escaping their subtable, even with later bytes present.
    truetype_test_u16(bytes, first_subtable + 6u, 2);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -30);
    truetype_test_u16(bytes, first_subtable + 6u, 1);
    truetype_test_u16(bytes, second_subtable + 6u, 2);
    // The following bytes resemble A/B,+200, but lie outside the second table.
    truetype_test_u16(bytes, second_subtable + 20u, 1);
    truetype_test_u16(bytes, second_subtable + 22u, 2);
    truetype_test_u16(bytes, second_subtable + 24u, 200);
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -20);
    truetype_test_u16(bytes, second_subtable + 6u, 1);
    font.data.length = second_subtable + 19u;
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -20);
    font.data.length = sizeof(bytes);
    truetype_test_u16(bytes, 32, 1); // unsupported table version
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == 0);
    truetype_test_u16(bytes, 32, 0);
    truetype_test_u16(bytes, second_subtable, 1); // unsupported subtable version
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == -20);
    font.kern = 0;
    BUSTER_TEST(arguments, truetype_get_codepoint_kern_advance(&font, 'A', 'B') == 0);
    return result;
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
            // Parent index == preceding point count + 4 phantoms, child index ==
            // child count + 4, first-component attachment past its phantoms,
            // empty child and a cycle all fail without retaining any partial
            // arena allocation.
            u32 second = 64u + 10u + 8u;
            s32 invalid_indices[][2] = {{8, 0}, {2, 7}, {65535, 0}, {2, 65535}};
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
            truetype_test_u16(bytes, 78, 4);
            TTF_Bitmap rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u16(bytes, 74, 35);
            truetype_test_u16(bytes, 78, 0);
            truetype_test_u16(bytes, 64u + offsets[3], 0);
            truetype_test_u16(bytes, second + 6u, 4);
            rejected = truetype_get_codepoint_bitmap(arena, &font, bitmap_scale, bitmap_scale, 0);
            BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            truetype_test_u16(bytes, 64u + offsets[3], 1);
            truetype_test_u16(bytes, second + 6u, 0);
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

// Builds a complete minimal TrueType font in bytes: one 700 by 700 unit square
// glyph that every codepoint from ' ' to '~' maps to. The hhea ascent and
// descent set truetype_scale_for_pixel_height, so shrinking them makes the
// rasterized glyphs arbitrarily larger than the atlas. Returns the byte length.
BUSTER_GLOBAL_LOCAL u32 truetype_test_atlas_font(u8* bytes, u32 capacity, s32 ascent, s32 descent)
{
    enum
    {
        atlas_table_count = 7,
        atlas_character_count = '~' - ' ' + 1,
        atlas_cmap_length = 12 + 16 + atlas_character_count * 12,
        atlas_head_length = 54,
        atlas_hhea_length = 36,
        atlas_hmtx_length = 8,
        atlas_maxp_length = 32,
        atlas_loca_length = 12,
    };
    typedef struct TruetypeAtlasTable TruetypeAtlasTable;
    struct TruetypeAtlasTable
    {
        char8 tag[4];
        u32 offset;
        u32 length;
    };
    u32 cmap = 12u + atlas_table_count * 16u;
    u32 head = cmap + atlas_cmap_length;
    u32 hhea = head + atlas_head_length;
    u32 hmtx = hhea + atlas_hhea_length;
    u32 maxp = hmtx + atlas_hmtx_length;
    u32 loca = maxp + atlas_maxp_length;
    u32 glyf = loca + atlas_loca_length;
    memset(bytes, 0, capacity);

    TruetypeSourceTestPoint square[] = {{0, 0, 1, {0}}, {700, 0, 1, {0}}, {700, 700, 1, {0}}, {0, 700, 1, {0}}};
    u32 end = truetype_test_simple_outline(bytes, glyf, square, BUSTER_ARRAY_LENGTH(square), 0);

    TruetypeAtlasTable tables[atlas_table_count] = {
        {{'c', 'm', 'a', 'p'}, cmap, atlas_cmap_length}, {{'h', 'e', 'a', 'd'}, head, atlas_head_length},
        {{'h', 'h', 'e', 'a'}, hhea, atlas_hhea_length}, {{'h', 'm', 't', 'x'}, hmtx, atlas_hmtx_length},
        {{'m', 'a', 'x', 'p'}, maxp, atlas_maxp_length}, {{'l', 'o', 'c', 'a'}, loca, atlas_loca_length},
        {{'g', 'l', 'y', 'f'}, glyf, end - glyf},
    };
    truetype_test_u32(bytes, 0, 0x00010000u);
    truetype_test_u16(bytes, 4, atlas_table_count);
    for (u32 index = 0; index < atlas_table_count; index += 1)
    {
        u32 record = 12u + index * 16u;
        memcpy(bytes + record, tables[index].tag, 4);
        truetype_test_u32(bytes, record + 8u, tables[index].offset);
        truetype_test_u32(bytes, record + 12u, tables[index].length);
    }

    // One Unicode (platform 3, encoding 10) format-12 subtable with a
    // single-codepoint group per character, all mapped to glyph 1.
    truetype_test_u16(bytes, cmap + 2u, 1);
    truetype_test_u16(bytes, cmap + 4u, 3);
    truetype_test_u16(bytes, cmap + 6u, 10);
    truetype_test_u32(bytes, cmap + 8u, 12);
    truetype_test_u16(bytes, cmap + 12u, 12);
    truetype_test_u32(bytes, cmap + 16u, 16u + atlas_character_count * 12u);
    truetype_test_u32(bytes, cmap + 24u, atlas_character_count);
    for (u32 index = 0; index < atlas_character_count; index += 1)
    {
        u32 group = cmap + 28u + index * 12u;
        truetype_test_u32(bytes, group, ' ' + index);
        truetype_test_u32(bytes, group + 4u, ' ' + index);
        truetype_test_u32(bytes, group + 8u, 1);
    }

    truetype_test_u16(bytes, head + 18u, 1000); // units per em
    truetype_test_u16(bytes, head + 50u, 1); // long loca
    truetype_test_u16(bytes, hhea + 4u, ascent);
    truetype_test_u16(bytes, hhea + 6u, descent);
    truetype_test_u16(bytes, hhea + 34u, 2); // horizontal metrics
    truetype_test_u16(bytes, hmtx, 500);
    truetype_test_u16(bytes, hmtx + 4u, 800);
    truetype_test_u16(bytes, maxp + 4u, 2); // glyphs
    truetype_test_u16(bytes, maxp + 6u, 4); // max points
    truetype_test_u16(bytes, maxp + 8u, 1); // max contours
    truetype_test_u32(bytes, loca + 8u, end - glyf); // glyph 0 is empty, glyph 1 is the square
    return end;
}

// font_texture_atlas_create copies glyph bitmaps whose sizes come from the font
// file into a fixed atlas. A font with a tiny hhea ascent - descent scales its
// glyphs far beyond the atlas; that must fail with a status in every build
// (the old BUSTER_CHECK bounds became optimizer assumptions in Release and the
// copy ran megabytes past the atlas). Text heights whose atlas size overflows
// u32 must be rejected too.
BUSTER_GLOBAL_LOCAL UnitTestResult truetype_test_font_atlas(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    u8 bytes[1536];
    enum
    {
        text_height = 16,
        atlas_edge = text_height * 16,
    };

    // Control: ascent - descent = 700 scales the square to 16 pixels.
    u32 length = truetype_test_atlas_font(bytes, sizeof(bytes), 700, 0);
    TTF_AtlasBuild build = truetype_font_atlas_build(arena, (ByteSlice){.pointer = bytes, .length = length}, text_height);
    BUSTER_TEST(arguments, build.status == TTF_ATLAS_SUCCESS && build.description.pointer && build.description.width == atlas_edge &&
                               build.description.height == atlas_edge);
    if (build.status == TTF_ATLAS_SUCCESS && build.description.pointer && build.description.characters)
    {
        const FontCharacter* letter = &build.description.characters['A'];
        BUSTER_TEST(arguments, letter->width >= 15 && letter->width <= 17 && letter->height == letter->width);
        BUSTER_TEST(arguments, build.description.pointer[letter->y * build.description.width + letter->x + 1] == 0xffffffffu);
        BUSTER_TEST(arguments, build.description.characters['~'].y + build.description.characters['~'].height <= atlas_edge);
    }
    arena_set_position(arena, position);

    // Glyphs of about 415 pixels cannot fit a 256 pixel atlas row.
    length = truetype_test_atlas_font(bytes, sizeof(bytes), 20, -7);
    build = truetype_font_atlas_build(arena, (ByteSlice){.pointer = bytes, .length = length}, text_height);
    BUSTER_TEST(arguments, build.status == TTF_ATLAS_GLYPH_DOES_NOT_FIT && !build.description.pointer && !build.description.characters &&
                               !build.description.width && !build.description.height);
    arena_set_position(arena, position);

    // Glyphs of about 238 pixels fit horizontally but the second row does not fit vertically.
    length = truetype_test_atlas_font(bytes, sizeof(bytes), 47, 0);
    build = truetype_font_atlas_build(arena, (ByteSlice){.pointer = bytes, .length = length}, text_height);
    BUSTER_TEST(arguments, build.status == TTF_ATLAS_GLYPH_DOES_NOT_FIT && !build.description.pointer);
    arena_set_position(arena, position);

    // The text height product overflowed u32 from 4096 on, producing a tiny atlas.
    u32 rejected_heights[] = {0, BUSTER_TTF_ATLAS_MAX_TEXT_HEIGHT + 1u, 4096, 65536, 0x80000000u, UINT32_MAX};
    length = truetype_test_atlas_font(bytes, sizeof(bytes), 700, 0);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejected_heights); index += 1)
    {
        build = truetype_font_atlas_build(arena, (ByteSlice){.pointer = bytes, .length = length}, rejected_heights[index]);
        BUSTER_TEST(arguments, build.status == TTF_ATLAS_INVALID_TEXT_HEIGHT && !build.description.pointer && arena->position == position);
    }

    // Truncated and empty input is an unusable font, not a partial atlas.
    build = truetype_font_atlas_build(arena, (ByteSlice){.pointer = bytes, .length = 40}, text_height);
    BUSTER_TEST(arguments, build.status == TTF_ATLAS_INVALID_FONT && !build.description.pointer);
    build = truetype_font_atlas_build(arena, (ByteSlice){0}, text_height);
    BUSTER_TEST(arguments, build.status == TTF_ATLAS_INVALID_FONT && !build.description.pointer);
    arena_set_position(arena, position);
    return result;
}

// System font discovery must only select a file this parser can use, and must
// say why every other candidate was passed over (font_provider.c reports it).
// Candidate files are synthesized: an 'OTTO' (CFF) sfnt, a TrueType collection
// whose first face is unreadable, a truncated font, an empty and a missing
// file, then a valid font.
BUSTER_GLOBAL_LOCAL UnitTestResult truetype_test_candidate_selection(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    u8 valid_bytes[1536];
    u32 valid_length = truetype_test_atlas_font(valid_bytes, sizeof(valid_bytes), 700, 0);
    u8 cff_bytes[1536];
    memcpy(cff_bytes, valid_bytes, valid_length);
    memcpy(cff_bytes, "OTTO", 4);
    u8 collection_bytes[64] = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 2};
    truetype_test_u32(collection_bytes, 12, 4000); // first face lies outside the file
    u8 truncated_bytes[16];
    memcpy(truncated_bytes, valid_bytes, sizeof(truncated_bytes));

    typedef struct CandidateFile CandidateFile;
    struct CandidateFile
    {
        String8 name;
        ByteSlice content;
        bool write;
        TTF_FontCandidateStatus expected;
    };
    CandidateFile files[] = {
        {S8("font-candidate-missing"), {0}, false, TTF_FONT_CANDIDATE_UNREADABLE},
        {S8("font-candidate-empty"), {0}, true, TTF_FONT_CANDIDATE_UNREADABLE},
        {S8("font-candidate-otto"), {cff_bytes, valid_length}, true, TTF_FONT_CANDIDATE_UNSUPPORTED},
        {S8("font-candidate-collection"), {collection_bytes, sizeof(collection_bytes)}, true, TTF_FONT_CANDIDATE_MALFORMED},
        {S8("font-candidate-truncated"), {truncated_bytes, sizeof(truncated_bytes)}, true, TTF_FONT_CANDIDATE_MALFORMED},
        {S8("font-candidate-valid"), {valid_bytes, valid_length}, true, TTF_FONT_CANDIDATE_USABLE},
        {S8("font-candidate-valid-later"), {valid_bytes, valid_length}, true, TTF_FONT_CANDIDATE_USABLE},
    };
    enum
    {
        file_count = 7,
        valid_index = 5,
    };
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(files) == file_count);
    String8 paths[file_count];
    for (u32 index = 0; index < file_count; index += 1)
    {
        paths[index] = buster_test_temporary_path(arena, files[index].name, S8(".ttf"));
        os_file_delete(paths[index]);
        if (files[index].write)
        {
            BUSTER_TEST(arguments, file_write(paths[index], files[index].content));
        }
    }

    // Unsupported candidates fall through to the first valid one, and the
    // search stops there: later candidates are not even read.
    TTF_FontCandidateStatus statuses[file_count] = {0};
    u64 selected = truetype_font_select_first_usable(paths, file_count, statuses);
    BUSTER_TEST(arguments, selected == valid_index);
    for (u32 index = 0; index < file_count; index += 1)
    {
        TTF_FontCandidateStatus expected = index > valid_index ? TTF_FONT_CANDIDATE_NOT_TRIED : files[index].expected;
        BUSTER_TEST(arguments, statuses[index] == expected);
    }

    // The first usable candidate wins even when it is first, and a null status array is allowed.
    BUSTER_TEST(arguments, truetype_font_select_first_usable(paths + valid_index, 2, 0) == 0);

    // When nothing is usable every path carries its own reason.
    memset(statuses, 0, sizeof(statuses));
    selected = truetype_font_select_first_usable(paths, valid_index, statuses);
    BUSTER_TEST(arguments, selected == valid_index);
    for (u32 index = 0; index < valid_index; index += 1)
    {
        BUSTER_TEST(arguments, statuses[index] == files[index].expected && statuses[index] != TTF_FONT_CANDIDATE_USABLE);
        BUSTER_TEST(arguments, truetype_font_candidate_status_description(statuses[index]).length != 0);
    }
    BUSTER_TEST(arguments, truetype_font_select_first_usable(paths, 0, statuses) == 0);
    String8 empty_path = {0};
    TTF_FontCandidateStatus empty_status = TTF_FONT_CANDIDATE_USABLE;
    BUSTER_TEST(arguments, truetype_font_select_first_usable(&empty_path, 1, &empty_status) == 1 && empty_status == TTF_FONT_CANDIDATE_UNREADABLE);

    // A font the atlas builder cannot use must be rejected by selection too: both ask truetype_font_initialize.
    for (u32 index = 0; index < valid_index; index += 1)
    {
        if (files[index].write && files[index].content.length)
        {
            TTF_AtlasBuild build = truetype_font_atlas_build(arena, files[index].content, 16);
            BUSTER_TEST(arguments, build.status == TTF_ATLAS_INVALID_FONT);
            arena_set_position(arena, position);
        }
    }

    for (u32 index = 0; index < file_count; index += 1)
    {
        os_file_delete(paths[index]);
    }
    arena_set_position(arena, position);
    return result;
}

// Phantom-point anchors. Builds a complete sfnt (cmap/head/hhea/hmtx/maxp/loca/
// glyf, plus vhea/vmtx when requested) whose composites attach to unhinted
// phantom points, and an XY-offset twin for every attachment. The twin offsets
// are derived by hand from these metrics:
//   B (glyph 1): box 0..100, advance 400, lsb 0      -> pp1 (0,0)  pp2 (400,0)
//   L (glyph 2): box 100..300 x 0..200, advance 500, lsb 40, vmtx tsb 30,
//                advance height 700                  -> pp1 (60,0) pp2 (560,0)
//   composites: header box (-100,-50)..(800,400), advance 450, lsb -120,
//                vmtx tsb 10, advance height 500      -> pp1 (20,0) pp2 (470,0)
// Without vmtx the vertical pair is the hhea ascent/descent (800/-200), so
// pp3 on pp3 shifts by 410 - 230 = 180 with vmtx and by 800 - 800 = 0 without.
enum
{
    TRUETYPE_PHANTOM_GLYPH_COUNT = 19,
    TRUETYPE_PHANTOM_COMPOSITE_COUNT = TRUETYPE_PHANTOM_GLYPH_COUNT - 3,
    TRUETYPE_PHANTOM_BYTES = 8192,
};

typedef struct TruetypePhantomComponent TruetypePhantomComponent;
struct TruetypePhantomComponent
{
    u16 flags;
    u16 glyph;
    s32 arg1;
    s32 arg2;
};

typedef struct TruetypePhantomTable TruetypePhantomTable;
struct TruetypePhantomTable
{
    char8 tag[4];
    u32 offset;
    u32 length;
};

BUSTER_GLOBAL_LOCAL u32 truetype_test_phantom_font(u8* bytes, bool with_vmtx)
{
    u32 glyph_count = TRUETYPE_PHANTOM_GLYPH_COUNT;
    u32 table_count = with_vmtx ? 9u : 7u;
    u32 cmap = 12u + table_count * 16u;
    u32 head = cmap + 40u;
    u32 hhea = head + 54u;
    u32 maxp = hhea + 36u;
    u32 hmtx = maxp + 32u;
    u32 vhea = hmtx + glyph_count * 4u;
    u32 vmtx = vhea + (with_vmtx ? 36u : 0u);
    u32 loca = vmtx + (with_vmtx ? glyph_count * 4u : 0u);
    u32 glyf = loca + (glyph_count + 1u) * 4u;
    memset(bytes, 0, TRUETYPE_PHANTOM_BYTES);

    TruetypeSourceTestPoint box_b[] = {{0, 0, 1, {0}}, {100, 0, 1, {0}}, {100, 100, 1, {0}}, {0, 100, 1, {0}}};
    TruetypeSourceTestPoint box_l[] = {{100, 0, 1, {0}}, {300, 0, 1, {0}}, {300, 200, 1, {0}}, {100, 200, 1, {0}}};
    s16 half[] = {8192};
    // Flags: 3 is XY words, 1 is point-number words, 8 is a scale, 512 is
    // USE_MY_METRICS; "more components" (32) is added while encoding.
    TruetypePhantomComponent composites[TRUETYPE_PHANTOM_COMPOSITE_COUNT][3] = {
        // 3/4: composite pp2 (470,0) with child L point 0 (100,0) == XY (370,0).
        {{3, 1, 0, 0}, {1, 2, 5, 0}},
        {{3, 1, 0, 0}, {3, 2, 370, 0}},
        // 5/6: child pp1 (index 4) on parent point 2 (100,100) == XY (40,100).
        {{3, 1, 0, 0}, {1, 2, 2, 4}},
        {{3, 1, 0, 0}, {3, 2, 40, 100}},
        // 7/8: composite pp2 (470,0) with child pp2 (560,0) == XY (-90,0).
        {{3, 1, 0, 0}, {1, 2, 5, 5}},
        {{3, 1, 0, 0}, {3, 2, -90, 0}},
        // 9/10: USE_MY_METRICS on L makes composite pp2 (index 9) L's (560,0).
        {{3, 1, 0, 0}, {3 | 512, 2, 300, 0}, {1, 1, 9, 0}},
        {{3, 1, 0, 0}, {3, 2, 300, 0}, {3, 1, 560, 0}},
        // 11/12: child pp2 under a 0.5 matrix is (280,0): XY (190,0).
        {{3, 1, 0, 0}, {1 | 8, 2, 5, 5}},
        {{3, 1, 0, 0}, {3 | 8, 2, 190, 0}},
        // 13/14: nested composite 3 attached by its own pp1 (20,0), index 8.
        {{3, 1, 0, 0}, {1, 3, 2, 8}},
        {{3, 1, 0, 0}, {3, 3, 80, 100}},
        // 15: parent index one past pp4. 16: child index one past pp4.
        {{3, 1, 0, 0}, {1, 2, 8, 0}},
        {{3, 1, 0, 0}, {1, 2, 2, 8}},
        // 17/18: pp3 on pp3, which depends on vmtx versus the hhea fallback.
        {{3, 1, 0, 0}, {1, 2, 6, 6}},
        {{3, 1, 0, 0}, {3, 2, -40, with_vmtx ? 180 : 0}},
    };
    u32 component_counts[TRUETYPE_PHANTOM_COMPOSITE_COUNT] = {2, 2, 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2};
    u32 offsets[TRUETYPE_PHANTOM_GLYPH_COUNT + 1u] = {0};
    u32 cursor = glyf;
    // Glyph 0 is empty, 1 is B, 2 is L and 3.. are the composites above.
    for (u32 glyph = 1; glyph < glyph_count; glyph += 1)
    {
        offsets[glyph] = cursor - glyf;
        u32 start = cursor;
        if (glyph < 3)
        {
            cursor = truetype_test_simple_outline(bytes, cursor, glyph == 1 ? box_b : box_l, 4, 0);
            truetype_test_u16(bytes, start + 2u, glyph == 1 ? 0 : 100);
            truetype_test_u16(bytes, start + 4u, 0);
            truetype_test_u16(bytes, start + 6u, glyph == 1 ? 100 : 300);
            truetype_test_u16(bytes, start + 8u, glyph == 1 ? 100 : 200);
        }
        else
        {
            u32 composite = glyph - 3u;
            truetype_test_compound_header(bytes, cursor);
            truetype_test_u16(bytes, start + 2u, -100);
            truetype_test_u16(bytes, start + 4u, -50);
            truetype_test_u16(bytes, start + 6u, 800);
            truetype_test_u16(bytes, start + 8u, 400);
            cursor += 10u;
            for (u32 component = 0; component < component_counts[composite]; component += 1)
            {
                TruetypePhantomComponent source = composites[composite][component];
                u16 flags = (u16)(source.flags | (component + 1u < component_counts[composite] ? 32u : 0u));
                cursor = truetype_test_component(bytes, cursor, flags, source.glyph, source.arg1, source.arg2, half);
            }
        }
    }
    offsets[glyph_count] = cursor - glyf;

    TruetypePhantomTable tables[9] = {
        {{'c', 'm', 'a', 'p'}, cmap, 40},
        {{'h', 'e', 'a', 'd'}, head, 54},
        {{'h', 'h', 'e', 'a'}, hhea, 36},
        {{'h', 'm', 't', 'x'}, hmtx, glyph_count * 4u},
        {{'m', 'a', 'x', 'p'}, maxp, 32},
        {{'l', 'o', 'c', 'a'}, loca, (glyph_count + 1u) * 4u},
        {{'g', 'l', 'y', 'f'}, glyf, cursor - glyf},
        {{'v', 'h', 'e', 'a'}, vhea, 36},
        {{'v', 'm', 't', 'x'}, vmtx, glyph_count * 4u},
    };
    truetype_test_u32(bytes, 0, 0x00010000u);
    truetype_test_u16(bytes, 4, (s32)table_count);
    for (u32 index = 0; index < table_count; index += 1)
    {
        u32 record = 12u + index * 16u;
        memcpy(bytes + record, tables[index].tag, 4);
        truetype_test_u32(bytes, record + 8u, tables[index].offset);
        truetype_test_u32(bytes, record + 12u, tables[index].length);
    }
    // One format-12 group maps codepoints 0..18 to glyphs 0..18.
    truetype_test_u16(bytes, cmap + 2u, 1);
    truetype_test_u16(bytes, cmap + 4u, 3);
    truetype_test_u16(bytes, cmap + 6u, 10);
    truetype_test_u32(bytes, cmap + 8u, 12);
    truetype_test_u16(bytes, cmap + 12u, 12);
    truetype_test_u32(bytes, cmap + 16u, 28);
    truetype_test_u32(bytes, cmap + 24u, 1);
    truetype_test_u32(bytes, cmap + 28u, 0);
    truetype_test_u32(bytes, cmap + 32u, glyph_count - 1u);
    truetype_test_u32(bytes, cmap + 36u, 0);
    truetype_test_u16(bytes, head + 18u, 1000);
    truetype_test_u16(bytes, head + 50u, 1);
    truetype_test_u16(bytes, hhea + 4u, 800);
    truetype_test_u16(bytes, hhea + 6u, -200);
    truetype_test_u16(bytes, hhea + 34u, (s32)glyph_count);
    truetype_test_u16(bytes, maxp + 4u, (s32)glyph_count);
    truetype_test_u16(bytes, maxp + 6u, 16);
    truetype_test_u16(bytes, maxp + 8u, 4);
    truetype_test_u16(bytes, maxp + 10u, 32);
    truetype_test_u16(bytes, maxp + 12u, 8);
    if (with_vmtx)
    {
        truetype_test_u16(bytes, vhea + 34u, (s32)glyph_count);
    }
    for (u32 glyph = 1; glyph < glyph_count; glyph += 1)
    {
        truetype_test_u16(bytes, hmtx + glyph * 4u, glyph == 1 ? 400 : (glyph == 2 ? 500 : 450));
        truetype_test_u16(bytes, hmtx + glyph * 4u + 2u, glyph == 1 ? 0 : (glyph == 2 ? 40 : -120));
        if (with_vmtx)
        {
            truetype_test_u16(bytes, vmtx + glyph * 4u, glyph == 2 ? 700 : 500);
            truetype_test_u16(bytes, vmtx + glyph * 4u + 2u, glyph == 2 ? 30 : 10);
        }
    }
    for (u32 glyph = 0; glyph <= glyph_count; glyph += 1)
    {
        truetype_test_u32(bytes, loca + glyph * 4u, offsets[glyph]);
    }
    return cursor;
}

// Rasterizes two codepoints and requires identical, non-empty bitmaps.
BUSTER_GLOBAL_LOCAL bool truetype_test_phantom_pair(Arena* arena, const TTF_FontInformation* font, u32 anchored, u32 twin)
{
    u64 position = arena->position;
    TTF_Bitmap a = truetype_get_codepoint_bitmap(arena, font, 0.1f, 0.1f, anchored);
    TTF_Bitmap b = truetype_get_codepoint_bitmap(arena, font, 0.1f, 0.1f, twin);
    bool same = a.pixels && b.pixels && a.width > 0 && a.height > 0 && a.width == b.width && a.height == b.height &&
                a.x_offset == b.x_offset && a.y_offset == b.y_offset &&
                memcmp(a.pixels, b.pixels, (size_t)a.width * (size_t)a.height) == 0;
    u32 coverage = 0;
    for (u32 pixel = 0; same && pixel < (u32)a.width * (u32)a.height; pixel += 1)
    {
        coverage += a.pixels[pixel];
    }
    arena_set_position(arena, position);
    bool result = same && coverage != 0;
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult truetype_test_phantom_attachments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    for (u32 variant = 0; variant < 2; variant += 1)
    {
        u8 bytes[TRUETYPE_PHANTOM_BYTES];
        u32 length = truetype_test_phantom_font(bytes, variant == 0);
        TTF_FontInitialization initialization = truetype_font_initialize((ByteSlice){.pointer = bytes, .length = length}, 0);
        if (BUSTER_REQUIRE(arguments, initialization.result == TTF_FONT_INITIALIZATION_SUCCESS))
        {
            const TTF_FontInformation* font = &initialization.information;
            BUSTER_TEST(arguments, (font->num_vmetrics != 0) == (variant == 0));
            // Codepoints equal glyph indices: 3 parent pp2, 5 child pp1, 7 both
            // composite pp2s, 9 USE_MY_METRICS, 11 transformed child, 13 nested
            // composite child pp1, 17 vertical pair (vmtx or hhea fallback).
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 3, 4));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 5, 6));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 7, 8));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 9, 10));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 11, 12));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 13, 14));
            BUSTER_TEST(arguments, truetype_test_phantom_pair(arena, font, 17, 18));
            // Parent and child indices one past pp4 are still refused.
            for (u32 codepoint = 15; codepoint <= 16; codepoint += 1)
            {
                TTF_Bitmap rejected = truetype_get_codepoint_bitmap(arena, font, 0.1f, 0.1f, codepoint);
                BUSTER_TEST(arguments, truetype_test_empty(rejected) && arena->position == position);
            }
        }
    }
    arena_set_position(arena, position);
    return result;
}

UnitTestResult truetype_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = truetype_test_compound_attachments(arguments);
    UnitTestResult phantom_result = truetype_test_phantom_attachments(arguments);
    result.test_count += phantom_result.test_count;
    result.succeeded_test_count += phantom_result.succeeded_test_count;
    UnitTestResult atlas_result = truetype_test_font_atlas(arguments);
    result.test_count += atlas_result.test_count;
    result.succeeded_test_count += atlas_result.succeeded_test_count;
    UnitTestResult selection_result = truetype_test_candidate_selection(arguments);
    result.test_count += selection_result.test_count;
    result.succeeded_test_count += selection_result.succeeded_test_count;
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
    UnitTestResult kerning_result = truetype_test_kerning(arguments);
    result.test_count += kerning_result.test_count;
    result.succeeded_test_count += kerning_result.succeeded_test_count;
    return result;
}
#endif
