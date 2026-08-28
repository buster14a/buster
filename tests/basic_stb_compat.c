// Deterministic, dependency-free probes for the selected stb headers.  The
// harness compiles this translation unit against each generated implementation
// set and compares its one-line result with a Clang reference.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_ds.h"
#include "stb_truetype.h"

typedef struct StbCapture StbCapture;
struct StbCapture
{
    unsigned char bytes[1u << 16];
    size_t length;
};

static void stb_capture_write(void *context, void *data, int size)
{
    StbCapture *capture = (StbCapture *)context;
    if (size > 0 && capture->length + (size_t)size <= sizeof(capture->bytes))
    {
        memcpy(capture->bytes + capture->length, data, (size_t)size);
        capture->length += (size_t)size;
    }
}

static uint64_t stb_hash_bytes(const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint64_t result = 1469598103934665603ull;
    for (size_t index = 0; index < length; index += 1)
    {
        result ^= bytes[index];
        result *= 1099511628211ull;
    }
    return result;
}

static uint64_t stb_hash_u64(uint64_t hash, uint64_t value)
{
    return stb_hash_bytes(&value, sizeof(value)) ^ (hash * 1099511628211ull);
}

static void stb_put_u16(unsigned char *bytes, size_t offset, uint16_t value)
{
    bytes[offset + 0] = (unsigned char)(value >> 8);
    bytes[offset + 1] = (unsigned char)value;
}

static void stb_put_s16(unsigned char *bytes, size_t offset, int16_t value)
{
    stb_put_u16(bytes, offset, (uint16_t)value);
}

static void stb_put_u32(unsigned char *bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0] = (unsigned char)(value >> 24);
    bytes[offset + 1] = (unsigned char)(value >> 16);
    bytes[offset + 2] = (unsigned char)(value >> 8);
    bytes[offset + 3] = (unsigned char)value;
}

// Build a tiny project-owned sfnt in memory.  It has six glyphs (notdef,
// space, @, A, a, g), a format-4 Unicode cmap, horizontal metrics, long loca,
// and four rectangular outlines.  No host font or filesystem data is needed.
static size_t stb_build_fixture_font(unsigned char *font, size_t capacity)
{
    enum { table_count = 7 };
    const size_t records = 12 + table_count * 16;
    const size_t cmap_offset = 128;
    const size_t head_offset = 196;
    const size_t hhea_offset = 252;
    const size_t maxp_offset = 292;
    const size_t hmtx_offset = 328;
    const size_t loca_offset = 376;
    const size_t glyf_offset = 408;
    const size_t cmap_length = 68;
    const size_t head_length = 54;
    const size_t hhea_length = 36;
    const size_t maxp_length = 32;
    const size_t hmtx_length = 24;
    const size_t loca_length = 28;
    const size_t glyf_capacity = 256;
    size_t result = 0;

    if (font && capacity >= glyf_offset + glyf_capacity)
    {
        memset(font, 0, capacity);
        stb_put_u32(font, 0, 0x00010000u);
        stb_put_u16(font, 4, (uint16_t)table_count);
        stb_put_u16(font, 6, 0);
        stb_put_u16(font, 8, 0);
        stb_put_u16(font, 10, 0);

        const unsigned char tags[table_count][4] = {
            {'c', 'm', 'a', 'p'}, {'h', 'e', 'a', 'd'}, {'h', 'h', 'e', 'a'}, {'m', 'a', 'x', 'p'},
            {'h', 'm', 't', 'x'}, {'l', 'o', 'c', 'a'}, {'g', 'l', 'y', 'f'},
        };
        const uint32_t offsets[table_count] = {(uint32_t)cmap_offset, (uint32_t)head_offset, (uint32_t)hhea_offset,
                                                (uint32_t)maxp_offset, (uint32_t)hmtx_offset, (uint32_t)loca_offset,
                                                (uint32_t)glyf_offset};
        const uint32_t lengths[table_count] = {(uint32_t)cmap_length, (uint32_t)head_length, (uint32_t)hhea_length,
                                                (uint32_t)maxp_length, (uint32_t)hmtx_length, (uint32_t)loca_length,
                                                (uint32_t)glyf_capacity};
        for (size_t table_index = 0; table_index < table_count; table_index += 1)
        {
            size_t record = 12 + table_index * 16;
            memcpy(font + record, tags[table_index], 4);
            stb_put_u32(font, record + 4, 0);
            stb_put_u32(font, record + 8, offsets[table_index]);
            stb_put_u32(font, record + 12, lengths[table_index]);
        }

        // cmap format 4, with segments [32], [64..65], [97], [103], sentinel.
        size_t cmap = cmap_offset;
        stb_put_u16(font, cmap + 0, 0);
        stb_put_u16(font, cmap + 2, 1);
        stb_put_u16(font, cmap + 4, 3);
        stb_put_u16(font, cmap + 6, 1);
        stb_put_u32(font, cmap + 8, 12);
        size_t format4 = cmap + 12;
        stb_put_u16(font, format4 + 0, 4);
        stb_put_u16(font, format4 + 2, (uint16_t)(cmap_length - 12));
        stb_put_u16(font, format4 + 4, 0);
        stb_put_u16(font, format4 + 6, 10);
        stb_put_u16(font, format4 + 8, 8);
        stb_put_u16(font, format4 + 10, 2);
        stb_put_u16(font, format4 + 12, 2);
        size_t end_codes = format4 + 14;
        const uint16_t ends[5] = {32, 65, 97, 103, 0xffff};
        const uint16_t starts[5] = {32, 64, 97, 103, 0xffff};
        const uint16_t deltas[5] = {(uint16_t)-31, (uint16_t)-62, (uint16_t)-93, (uint16_t)-98, 1};
        for (size_t segment = 0; segment < 5; segment += 1)
        {
            stb_put_u16(font, end_codes + segment * 2, ends[segment]);
        }
        stb_put_u16(font, end_codes + 10, 0);
        size_t start_codes = end_codes + 12;
        size_t id_deltas = start_codes + 10;
        size_t id_ranges = id_deltas + 10;
        for (size_t segment = 0; segment < 5; segment += 1)
        {
            stb_put_u16(font, start_codes + segment * 2, starts[segment]);
            stb_put_u16(font, id_deltas + segment * 2, deltas[segment]);
            stb_put_u16(font, id_ranges + segment * 2, 0);
        }

        // head
        stb_put_u32(font, head_offset + 0, 0x00010000u);
        stb_put_u32(font, head_offset + 4, 0x00010000u);
        stb_put_u32(font, head_offset + 8, 0);
        stb_put_u32(font, head_offset + 12, 0x5f0f3cf5u);
        stb_put_u16(font, head_offset + 16, 0);
        stb_put_u16(font, head_offset + 18, 1000);
        stb_put_u32(font, head_offset + 20, 0);
        stb_put_u32(font, head_offset + 24, 0);
        stb_put_s16(font, head_offset + 36, 0);
        stb_put_s16(font, head_offset + 38, 0);
        stb_put_s16(font, head_offset + 40, 600);
        stb_put_s16(font, head_offset + 42, 700);
        stb_put_u16(font, head_offset + 44, 0);
        stb_put_u16(font, head_offset + 46, 8);
        stb_put_s16(font, head_offset + 48, 2);
        stb_put_s16(font, head_offset + 50, 1);
        stb_put_s16(font, head_offset + 52, 0);

        // hhea
        stb_put_u32(font, hhea_offset + 0, 0x00010000u);
        stb_put_s16(font, hhea_offset + 4, 800);
        stb_put_s16(font, hhea_offset + 6, -200);
        stb_put_s16(font, hhea_offset + 8, 0);
        stb_put_u16(font, hhea_offset + 10, 600);
        stb_put_s16(font, hhea_offset + 12, 0);
        stb_put_s16(font, hhea_offset + 14, 0);
        stb_put_s16(font, hhea_offset + 16, 600);
        stb_put_s16(font, hhea_offset + 18, 1);
        stb_put_s16(font, hhea_offset + 20, 0);
        stb_put_s16(font, hhea_offset + 22, 0);
        stb_put_s16(font, hhea_offset + 24, 0);
        stb_put_s16(font, hhea_offset + 26, 0);
        stb_put_s16(font, hhea_offset + 28, 0);
        stb_put_s16(font, hhea_offset + 30, 0);
        stb_put_u16(font, hhea_offset + 34, 6);

        // maxp
        stb_put_u32(font, maxp_offset + 0, 0x00010000u);
        stb_put_u16(font, maxp_offset + 4, 6);
        stb_put_u16(font, maxp_offset + 6, 4);
        stb_put_u16(font, maxp_offset + 8, 1);

        // six long horizontal metrics, including the empty space glyph.
        for (size_t glyph = 0; glyph < 6; glyph += 1)
        {
            stb_put_u16(font, hmtx_offset + glyph * 4 + 0, glyph == 1 ? 300 : 600);
            stb_put_s16(font, hmtx_offset + glyph * 4 + 2, 0);
        }

        // Simple four-point rectangles for @, A, a and g (glyphs 2..5).
        size_t glyph_cursor = glyf_offset;
        uint32_t glyph_offsets[7] = {0};
        const uint16_t widths[4] = {500, 600, 500, 500};
        const uint16_t heights[4] = {700, 700, 500, 700};
        for (size_t glyph = 0; glyph < 6; glyph += 1)
        {
            glyph_offsets[glyph] = (uint32_t)(glyph_cursor - glyf_offset);
            if (glyph >= 2)
            {
                uint16_t width = widths[glyph - 2];
                uint16_t height = heights[glyph - 2];
                stb_put_s16(font, glyph_cursor + 0, 1);
                stb_put_s16(font, glyph_cursor + 2, 0);
                stb_put_s16(font, glyph_cursor + 4, 0);
                stb_put_s16(font, glyph_cursor + 6, (int16_t)width);
                stb_put_s16(font, glyph_cursor + 8, (int16_t)height);
                stb_put_u16(font, glyph_cursor + 10, 3);
                stb_put_u16(font, glyph_cursor + 12, 0);
                font[glyph_cursor + 14] = 0x31;
                font[glyph_cursor + 15] = 0x13;
                font[glyph_cursor + 16] = 0x25;
                font[glyph_cursor + 17] = 0x23;
                font[glyph_cursor + 18] = (unsigned char)width;
                font[glyph_cursor + 19] = (unsigned char)height;
                font[glyph_cursor + 20] = (unsigned char)width;
                glyph_cursor += 21;
            }
        }
        glyph_offsets[6] = (uint32_t)(glyph_cursor - glyf_offset);
        for (size_t glyph = 0; glyph < 7; glyph += 1)
        {
            stb_put_u32(font, loca_offset + glyph * 4, glyph_offsets[glyph]);
        }
        result = glyf_offset + glyph_cursor - glyf_offset;
    }
    return result;
}

static int stb_run_probe(void)
{
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x08, 0x06, 0x00, 0x00, 0x00, 0xb4, 0xf4, 0xae, 0xc6,
        0x00, 0x00, 0x00, 0x3e, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x01, 0x33, 0x00, 0xcc, 0xff, 0x00,
        0x00, 0x00, 0x00, 0xff, 0x35, 0x1d, 0x47, 0xff, 0x6a, 0x3a, 0x8e, 0xff, 0x9f, 0x57, 0xd5, 0xff,
        0x00, 0x11, 0x1f, 0x07, 0xff, 0x46, 0x3c, 0x4e, 0xff, 0x7b, 0x59, 0x95, 0xff, 0xb0, 0x76, 0xdc, 0xff,
        0x00, 0x22, 0x3e, 0x0e, 0xff, 0x57, 0x5b, 0x55, 0xff, 0x8c, 0x78, 0x9c, 0xff, 0xc1, 0x95, 0xe3, 0xff,
        0x38, 0x0d, 0x19, 0x4b, 0x0a, 0x17, 0x89, 0x62, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(png, (int)sizeof(png), &width, &height, &channels, 4);
    int result = pixels && width == 4 && height == 3 && channels == 4;
    uint64_t pixel_hash = result ? stb_hash_bytes(pixels, (size_t)width * (size_t)height * 4u) : 0;

    StbCapture encoded_png = {0};
    StbCapture encoded_bmp = {0};
    StbCapture encoded_tga = {0};
    if (result)
    {
        result = stbi_write_png_to_func(stb_capture_write, &encoded_png, width, height, 4, pixels, width * 4) &&
                 stbi_write_bmp_to_func(stb_capture_write, &encoded_bmp, width, height, 4, pixels) &&
                 stbi_write_tga_to_func(stb_capture_write, &encoded_tga, width, height, 4, pixels);
    }

    static unsigned char font[4096];
    size_t font_length = stb_build_fixture_font(font, sizeof(font));
    stbtt_fontinfo font_info;
    result = result && font_length && stbtt_InitFont(&font_info, font, stbtt_GetFontOffsetForIndex(font, 0));
    uint64_t glyph_hash = 0;
    const int codepoints[] = {'@', 'A', 'a', 'g'};
    for (size_t index = 0; result && index < sizeof(codepoints) / sizeof(codepoints[0]); index += 1)
    {
        int glyph_width = 0;
        int glyph_height = 0;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&font_info, 0.0f, stbtt_ScaleForPixelHeight(&font_info, 32.0f), codepoints[index],
                                                         &glyph_width, &glyph_height, 0, 0);
        result = bitmap != 0 && glyph_width > 0 && glyph_height > 0;
        if (result)
        {
            glyph_hash = stb_hash_u64(glyph_hash, (uint64_t)(unsigned)codepoints[index]);
            glyph_hash = stb_hash_u64(glyph_hash, (uint64_t)(unsigned)glyph_width);
            glyph_hash = stb_hash_u64(glyph_hash, (uint64_t)(unsigned)glyph_height);
            glyph_hash = stb_hash_u64(glyph_hash, stb_hash_bytes(bitmap, (size_t)glyph_width * (size_t)glyph_height));
        }
        stbtt_FreeBitmap(bitmap, 0);
    }

    int *array = 0;
    arrsetcap(array, 32);
    for (int value = 0; value < 32; value += 1)
    {
        arrput(array, value * value + 3);
    }
    struct StbMapEntry
    {
        int key;
        int value;
    };
    struct StbMapEntry *map = 0;
    hmput(map, 17, 11);
    hmput(map, 23, 22);
    hmput(map, 41, 33);
    uint64_t ds_hash = stb_hash_u64(0, (uint64_t)arrlen(array));
    ds_hash = stb_hash_u64(ds_hash, (uint64_t)array[17]);
    ds_hash = stb_hash_u64(ds_hash, (uint64_t)hmlen(map));
    ds_hash = stb_hash_u64(ds_hash, (uint64_t)hmget(map, 17));
    ds_hash = stb_hash_u64(ds_hash, (uint64_t)hmget(map, 23));
    ds_hash = stb_hash_u64(ds_hash, (uint64_t)hmget(map, 41));
    arrfree(array);
    hmfree(map);
    if (pixels)
    {
        stbi_image_free(pixels);
    }

    printf("STB_PROBE decode=%d width=%d height=%d channels=%d pixel_hash=%016llx png_bytes=%zu png_hash=%016llx bmp_bytes=%zu bmp_hash=%016llx tga_bytes=%zu tga_hash=%016llx glyph_hash=%016llx ds_hash=%016llx\n",
           result, width, height, channels, (unsigned long long)pixel_hash, encoded_png.length, (unsigned long long)stb_hash_bytes(encoded_png.bytes, encoded_png.length),
           encoded_bmp.length, (unsigned long long)stb_hash_bytes(encoded_bmp.bytes, encoded_bmp.length), encoded_tga.length,
           (unsigned long long)stb_hash_bytes(encoded_tga.bytes, encoded_tga.length), (unsigned long long)glyph_hash, (unsigned long long)ds_hash);
    return result ? 0 : 1;
}

int main(int argc, char **argv)
{
    int result = 1;
    if (argc == 3 && strcmp(argv[1], "--write-font") == 0)
    {
        unsigned char font[4096];
        size_t length = stb_build_fixture_font(font, sizeof(font));
        FILE *file = fopen(argv[2], "wb");
        if (file && length)
        {
            result = fwrite(font, 1, length, file) == length ? 0 : 1;
            fclose(file);
        }
    }
    else if (argc == 1)
    {
        result = stb_run_probe();
    }
    return result;
}
