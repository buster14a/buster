#pragma once

#include <buster/font_provider.h>
#include <buster/file.h>
#include <buster/arena.h>
#include <buster/system_headers.h>

#if defined(__linux__)
#include <fontconfig/fontconfig.h>
#endif

BUSTER_GLOBAL_LOCAL bool font_config_initialized = false;

BUSTER_IMPL StringOs font_file_get_path(Arena* arena, FontIndex index)
{
    BUSTER_GLOBAL_LOCAL StringOs table[FONT_INDEX_COUNT] = {};
    if (!font_config_initialized)
    {
        font_config_initialized = true;
#if defined(__linux__)
        if (FcInit())
        {
            FcPattern *pat = FcPatternCreate();
            if (pat)
            {
                const char* family = "Fira Code";
                const char* style = "Regular";
                FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
                // Try to request "Regular" but allow fontconfig to substitute
                if (style && style[0]) {
                    FcPatternAddString(pat, FC_STYLE, (const FcChar8*)style);
                }

                FcConfigSubstitute(NULL, pat, FcMatchPattern);
                FcDefaultSubstitute(pat);

                FcResult result = FcResultNoMatch;
                FcPattern *match = FcFontMatch(NULL, pat, &result);

                if (match) {
                    FcChar8 *file = NULL;
                    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
                    {
                        table[FONT_INDEX_MONO] = string8_duplicate_arena(arena, string8_from_pointer((char*)file), true);
                    }
                    FcPatternDestroy(match);
                }

                FcPatternDestroy(pat);
            }
            FcFini();
        }
#else
#endif
    }

    static_assert(BUSTER_ARRAY_LENGTH(table) == FONT_INDEX_COUNT);
//     StringOs mono_path =
// #if defined(_WIN32)
//     SOs("C:/Users/David/Downloads/Fira_Sans/FiraSans-Regular.ttf");
// #elif defined(__linux__)
//     SOs("/usr/share/fonts/TTF/FiraSans-Regular.ttf");
// #elif defined(__APPLE__)
//     SOs("/Users/david/Library/Fonts/FiraSans-Regular.ttf");
// #else
//     SOs("WRONG_PATH");
// #endif


    return table[index];
}

#define USE_STB_TRUETYPE 1

#if USE_STB_TRUETYPE

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define stbtt_uint8  u8
#define stbtt_uint16 u16
#define stbtt_uint32 u32
#define stbtt_int8  s8
#define stbtt_int16 s16
#define stbtt_int32 s32

#include <buster/stb_truetype.h>

BUSTER_IMPL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    FontTextureAtlasDescription result = {};
    let font_file = file_read(arena, create.font_path, (FileReadOptions){});
    stbtt_fontinfo font_info;
    if (!stbtt_InitFont(&font_info, font_file.pointer, stbtt_GetFontOffsetForIndex(font_file.pointer, 0)))
    {
        os_fail();
    }

    u32 character_count = 256;
    result.characters = arena_allocate(arena, FontCharacter, character_count);
    result.kerning_tables = arena_allocate(arena, s32, character_count * character_count);
    result.height = (u32)sqrtf((f32)(create.text_height * create.text_height * character_count));
    result.width = result.height;
    result.pointer = arena_allocate(arena, u32, result.width * result.height);
    let scale_factor = stbtt_ScaleForPixelHeight(&font_info, (f32)create.text_height);

    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

    result.ascent = (int)roundf((f32)ascent * scale_factor);
    result.descent = (int)roundf((f32)descent * scale_factor);
    result.line_gap = (int)roundf((f32)line_gap * scale_factor);

    u32 x = 0;
    u32 y = 0;
    u32 max_row_height = 0;
    u32 first_character = ' ';
    u32 last_character = '~';

    for (let i = first_character; i <= last_character; i += 1)
    {
        u32 width;
        u32 height;
        int advance;
        int left_bearing;

        let ch = i;
        let character = &result.characters[i];
        stbtt_GetCodepointHMetrics(&font_info, (int)ch, &advance, &left_bearing);

        character->advance = (u32)roundf((float)advance * scale_factor);
        character->left_bearing = (u32)roundf((float)left_bearing * scale_factor);

        u8* bitmap = stbtt_GetCodepointBitmap(&font_info, 0.0f, scale_factor, (int)ch, (int*)&width, (int*)&height, &character->x_offset, &character->y_offset);
        let kerning_table = result.kerning_tables + i * character_count;
        for (u32 j = first_character; j <= last_character; j += 1)
        {
            let kerning_advance = stbtt_GetCodepointKernAdvance(&font_info, (int)i, (int)j);
            kerning_table[j] = (s32)roundf((float)kerning_advance * scale_factor);
        }

        if (x + width > result.width)
        {
            y += max_row_height;
            max_row_height = height;
            x = 0;
        }
        else
        {
            max_row_height = BUSTER_MAX(height, max_row_height);
        }

        character->x = x;
        character->y = y;
        character->width = width;
        character->height = height;

        let source = bitmap;
        let destination = result.pointer;

        for (u32 bitmap_y = 0; bitmap_y < height; bitmap_y += 1)
        {
            for (u32 bitmap_x = 0; bitmap_x < width; bitmap_x += 1)
            {
                let source_index = bitmap_y * width + bitmap_x;
                let destination_index = (bitmap_y + y) * result.width + (bitmap_x + x);
                let value = source[source_index];
                destination[destination_index] = ((u32)value << 24) | 0xffffff;
            }
        }

        x += width;

        stbtt_FreeBitmap(bitmap, 0);
    }

    return result;
}
#else
#include <buster/truetype.h>

BUSTER_IMPL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    FontTextureAtlasDescription result = {};

    let font_file = file_read(arena, create.font_path, (FileReadOptions){});
    let font_initialization = truetype_font_initialize(font_file, 0);
    let font_information = font_initialization.information;

    if (font_initialization.result == TTF_FONT_INITIALIZATION_SUCCESS)
    {
        u32 character_count = UINT8_MAX + 1;
        result.characters = arena_allocate(arena, FontCharacter, character_count);
        result.kerning_tables = arena_allocate(arena, s32, character_count * character_count);
        result.height = (u32)sqrtf((f32)(create.text_height * create.text_height * character_count));
        result.width = result.height;
        result.pointer = arena_allocate(arena, u32, result.width * result.height);
        let scale_factor = truetype_scale_for_pixel_height(&font_information, (f32)create.text_height);

        let vertical_metrics = truetype_get_font_vertical_metrics(&font_information);

        result.ascent = (s32)roundf(vertical_metrics.ascent * scale_factor);
        result.descent = (s32)roundf(vertical_metrics.descent * scale_factor);
        result.line_gap = (s32)roundf(vertical_metrics.line_gap * scale_factor);

        u32 x = 0;
        u32 y = 0;
        u32 max_row_height = 0;
        u32 first_character = ' ';
        u32 last_character = '~';

        let loop_start_position = arena->position;

        for (let i = first_character; i <= last_character; i += 1)
        {
            let ch = (u8)i;
            let character = &result.characters[i];
            let horizontal_metrics = truetype_get_codepoint_horizontal_metrics(&font_information, ch);

            character->advance = (u32)roundf((f32)horizontal_metrics.advance_width * scale_factor);
            character->left_bearing = (u32)roundf((f32)horizontal_metrics.left_side_bearing * scale_factor);

            let bitmap = truetype_get_codepoint_bitmap(arena, &font_information, scale_factor, scale_factor, ch);

            {
                let kerning_table = result.kerning_tables + i * character_count;

                for (u32 j = first_character; j <= last_character; j += 1)
                {
                    let kerning_advance = truetype_get_codepoint_kern_advance(&font_information, i, j);
                    kerning_table[j] = (s32)roundf((f32)kerning_advance * scale_factor);
                }
            }

            if ((x + (u32)bitmap.width) > result.width)
            {
                y += max_row_height;
                max_row_height = (u32)bitmap.height;
                x = 0;
            }
            else
            {
                max_row_height = BUSTER_MAX((u32)bitmap.height, (u32)max_row_height);
            }

            character->x = x;
            character->y = y;
            character->width = (u32)bitmap.width;
            character->height = (u32)bitmap.height;
            character->x_offset = bitmap.x_offset;
            character->y_offset = bitmap.y_offset;

            let source = bitmap;
            let destination = result.pointer;

            for (u32 bitmap_y = 0; bitmap_y < (u32)bitmap.height; bitmap_y += 1)
            {
                for (u32 bitmap_x = 0; bitmap_x < (u32)bitmap.width; bitmap_x += 1)
                {
                    let source_index = bitmap_y * (u32)bitmap.width + bitmap_x;
                    let destination_index = (bitmap_y + y) * result.width + (bitmap_x + x);
                    let value = source.pixels[source_index];
                    destination[destination_index] = ((u32)value << 24) | 0xffffff;
                }
            }

            x += (u32)bitmap.width;

            arena->position = loop_start_position;
        }
    }

    return result;
}
#endif
