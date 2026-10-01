// Headless TrueType bitmap admission tests. truetype_tests uses a bounded,
// synthetic one-contour glyph, so every platform checks identical bytes.
// The parameter sweep also runs in CI's sanitizer and fuzz-enabled trees.
#include <buster/tests/truetype_test.h>

#if BUSTER_INCLUDE_TESTS
// truetype_test_kerning covers subtable composition using synthetic cmap/kern bytes.
#include <buster/lib/truetype_internal.h>

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

UnitTestResult truetype_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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
