#include <buster/lib/truetype.c>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef BUSTER_TTF_MAX_RASTER_POINTS
#define BUSTER_TTF_MAX_RASTER_POINTS 1048576u
#endif

BUSTER_NORETURN BUSTER_COLD void os_fail_va(u32 line, String8 function, String8 file, String8 context, ...)
{
    BUSTER_UNUSED(line);
    BUSTER_UNUSED(function);
    BUSTER_UNUSED(file);
    BUSTER_UNUSED(context);
    abort();
}

BUSTER_COLD bool is_debugger_present(void)
{
    return false;
}

u64 align_forward(u64 value, u64 alignment)
{
    u64 result = (value + alignment - 1u) & ~(alignment - 1u);
    return result;
}

void string_print(String8 format, ...)
{
    BUSTER_UNUSED(format);
}

BUSTER_NORETURN void arena_allocation_overflow(void)
{
    abort();
}

void arena_allocate_commit(Arena* arena, u64 aligned_size_after)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(aligned_size_after);
    abort();
}

void arena_set_position(Arena* arena, u64 position)
{
    arena->position = position;
}

f32 floor_f32(f32 value)
{
    return floorf(value);
}

f32 ceil_f32(f32 value)
{
    return ceilf(value);
}

f32 sqrt_f32(f32 value)
{
    return sqrtf(value);
}

typedef struct BenchmarkFont BenchmarkFont;
struct BenchmarkFont
{
    char const* path;
    u32 first_codepoint;
    u32 codepoint_count;
    u8* bytes;
    TTF_FontInformation information;
};

static u64 timestamp_ns(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (u64)time.tv_sec * 1000000000ull + (u64)time.tv_nsec;
}

static bool benchmark_font_load(BenchmarkFont* font)
{
    bool result = false;
    FILE* file = fopen(font->path, "rb");
    if (file)
    {
        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (length > 0)
        {
            font->bytes = malloc((size_t)length);
            if (font->bytes && fread(font->bytes, 1, (size_t)length, file) == (size_t)length)
            {
                TTF_FontInitialization initialization = truetype_font_initialize((ByteSlice){.pointer = font->bytes, .length = (u64)length}, 0);
                if (initialization.result == TTF_FONT_INITIALIZATION_SUCCESS)
                {
                    font->information = initialization.information;
                    result = true;
                }
            }
        }
        fclose(file);
    }
    return result;
}

static u32 benchmark_glyph_segments(Arena* arena, TTF_RasterPoint* points, u32* contour_ends, const BenchmarkFont* font, f32 scale, u32 codepoint)
{
    u32 result = 0;
    u32 glyph = truetype_glyph_index_from_codepoint(&font->information, codepoint);
    TTF_GlyphRange range = truetype_glyph_range(&font->information, glyph);
    TTF_Bitmap box = ttf_bitmap_box(&font->information, range, scale, scale);
    if (box.width != 0 && box.height != 0)
    {
        TTF_RasterPath path = {
            .points = points,
            .contour_ends = contour_ends,
            .point_capacity = BUSTER_TTF_MAX_RASTER_POINTS,
            .contour_capacity = 65536,
        };
        TTF_Transform identity = {.m00 = 1.0f, .m11 = 1.0f};
        if (ttf_append_glyph_path(arena, &font->information, glyph, identity, scale, scale, box.x_offset, box.y_offset, 0, &path) && !path.overflowed)
        {
            result = path.point_count;
        }
    }
    arena->position = arena_minimum_position;
    return result;
}

static u64 benchmark_raster_round(Arena* arena, const BenchmarkFont* fonts, u32 font_count, f32 pixel_height, u64* digest)
{
    u64 start = timestamp_ns();
    for (u32 font_index = 0; font_index < font_count; font_index += 1)
    {
        const BenchmarkFont* font = &fonts[font_index];
        f32 scale = truetype_scale_for_pixel_height(&font->information, pixel_height);
        for (u32 codepoint_index = 0; codepoint_index < font->codepoint_count; codepoint_index += 1)
        {
            u32 codepoint = font->first_codepoint + codepoint_index;
            TTF_Bitmap bitmap = truetype_get_codepoint_bitmap(arena, &font->information, scale, scale, codepoint);
            if (bitmap.pixels)
            {
                u64 pixel_count = (u64)(u32)bitmap.width * (u64)(u32)bitmap.height;
                *digest = (*digest * 1099511628211ull) ^ bitmap.pixels[pixel_count / 2u];
            }
            arena->position = arena_minimum_position;
        }
    }
    u64 result = timestamp_ns() - start;
    return result;
}

static void benchmark_sort(u64* values, u32 count)
{
    for (u32 index = 1; index < count; index += 1)
    {
        u64 value = values[index];
        u32 target = index;
        while (target != 0 && values[target - 1u] > value)
        {
            values[target] = values[target - 1u];
            target -= 1u;
        }
        values[target] = value;
    }
}

int main(void)
{
    int result = 1;
    BenchmarkFont fonts[] = {
        {.path = "/usr/share/fonts/noto/NotoSans-Regular.ttf", .first_codepoint = 0x20, .codepoint_count = 64},
        {.path = "/usr/share/fonts/noto/NotoSerif-Regular.ttf", .first_codepoint = 0x20, .codepoint_count = 64},
        {.path = "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf", .first_codepoint = 0x0620, .codepoint_count = 64},
        {.path = "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf", .first_codepoint = 0x0900, .codepoint_count = 64},
        {.path = "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf", .first_codepoint = 0x2190, .codepoint_count = 64},
    };
    bool loaded = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fonts); index += 1)
    {
        loaded = benchmark_font_load(&fonts[index]) && loaded;
    }
    u64 arena_capacity = BUSTER_MB(256);
    Arena* arena = aligned_alloc(64, (size_t)arena_capacity);
    TTF_RasterPoint* points = malloc(sizeof(*points) * BUSTER_TTF_MAX_RASTER_POINTS);
    u32* contour_ends = malloc(sizeof(*contour_ends) * 65536u);
    if (loaded && arena && points && contour_ends)
    {
        *arena = (Arena){.reserved_size = arena_capacity, .position = arena_minimum_position, .os_position = arena_capacity};
        f32 pixel_heights[] = {12.0f, 32.0f, 128.0f};
        for (u32 height_index = 0; height_index < BUSTER_ARRAY_LENGTH(pixel_heights); height_index += 1)
        {
            u64 segment_count = 0;
            for (u32 font_index = 0; font_index < BUSTER_ARRAY_LENGTH(fonts); font_index += 1)
            {
                BenchmarkFont* font = &fonts[font_index];
                f32 scale = truetype_scale_for_pixel_height(&font->information, pixel_heights[height_index]);
                for (u32 codepoint_index = 0; codepoint_index < font->codepoint_count; codepoint_index += 1)
                {
                    segment_count += benchmark_glyph_segments(arena, points, contour_ends, font, scale, font->first_codepoint + codepoint_index);
                }
            }
            u64 digest = 14695981039346656037ull;
            for (u32 warmup = 0; warmup < 2; warmup += 1)
            {
                benchmark_raster_round(arena, fonts, BUSTER_ARRAY_LENGTH(fonts), pixel_heights[height_index], &digest);
            }
            u64 timings[9];
            for (u32 sample = 0; sample < BUSTER_ARRAY_LENGTH(timings); sample += 1)
            {
                timings[sample] = benchmark_raster_round(arena, fonts, BUSTER_ARRAY_LENGTH(fonts), pixel_heights[height_index], &digest);
            }
            benchmark_sort(timings, BUSTER_ARRAY_LENGTH(timings));
            printf("height=%.0f glyphs=320 segments=%llu min_ns=%llu median_ns=%llu digest=%llu\n", (double)pixel_heights[height_index],
                   (unsigned long long)segment_count, (unsigned long long)timings[0], (unsigned long long)timings[4], (unsigned long long)digest);
        }
        result = 0;
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fonts); index += 1)
    {
        free(fonts[index].bytes);
    }
    free(contour_ends);
    free(points);
    free(arena);
    return result;
}
