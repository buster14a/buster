// Headless TrueType bitmap admission tests. truetype_tests uses a bounded,
// synthetic one-contour glyph, so every platform checks identical bytes.
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
    BUSTER_TEST(arguments, !truetype_bitmap_work_is_valid_for_test(0, 1, 1));
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
