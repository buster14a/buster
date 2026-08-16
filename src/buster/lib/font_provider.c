// System font discovery for the retained UI stack: resolves platform font
// paths (fontconfig on Linux, the system font folders on Windows/macOS,
// bundled resources on iOS) into files truetype.c can parse. Resolved
// paths are a
// lazily built global; font_provider_prewarm fills them serially before
// parallel readers (AGENTS.md).

#include <buster/lib/font_provider.h>
#include <buster/lib/file.h>
#include <buster/lib/arena.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/float.h>
#include <buster/lib/string.h>

#ifndef BUSTER_USE_FONTCONFIG
#if BUSTER_LINUX
#define BUSTER_USE_FONTCONFIG 1
#else
#define BUSTER_USE_FONTCONFIG 0
#endif
#endif

#if BUSTER_USE_FONTCONFIG && defined(__TINYC__) && BUSTER_LINUX && !defined(__GLIBC__)
typedef unsigned char FcChar8;
typedef int FcBool;
typedef struct _FcConfig FcConfig;
typedef struct _FcPattern FcPattern;
typedef enum FcMatchKind
{
    FcMatchPattern,
    FcMatchFont,
    FcMatchScan,
} FcMatchKind;
typedef enum FcResult
{
    FcResultMatch,
    FcResultNoMatch,
    FcResultTypeMismatch,
    FcResultNoId,
    FcResultOutOfMemory,
} FcResult;
#define FC_FAMILY "family"
#define FC_STYLE "style"
#define FC_FILE "file"
extern FcBool FcInit(void);
extern void FcFini(void);
extern FcPattern* FcPatternCreate(void);
extern void FcPatternDestroy(FcPattern* p);
extern FcBool FcPatternAddString(FcPattern* p, const char* object, const FcChar8* s);
extern FcBool FcConfigSubstitute(FcConfig* config, FcPattern* p, FcMatchKind kind);
extern void FcDefaultSubstitute(FcPattern* pattern);
extern FcPattern* FcFontMatch(FcConfig* config, FcPattern* p, FcResult* result);
extern FcResult FcPatternGetString(const FcPattern* p, const char* object, int n, FcChar8** s);
#elif BUSTER_USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#define BUSTER_FONT_CANDIDATE_CAPACITY 32u

BUSTER_GLOBAL_LOCAL bool font_config_initialized = false;

BUSTER_GLOBAL_LOCAL bool font_path_is_usable(String8 path)
{
    bool result = false;
    if (path.pointer && path.length)
    {
#if BUSTER_IOS
        // iOS font paths are resources in the application bundle. file_read()
        // knows how to resolve those relative paths, while os_file_open() only
        // sees the process working directory.
        if (path.pointer[0] != '/')
        {
            ByteSlice file = file_read(os_state.arena, path, (FileReadOptions){0});
            result = file.pointer != 0 && file.length != 0;
        }
        else
#endif
        {
            OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
            if (file)
            {
                result = os_file_get_size(file) != 0;
                os_file_close(file);
            }
        }
    }
    return result;
}

#if BUSTER_WINDOWS || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL String8 font_path_join(Arena* arena, String8 directory, String8 file)
{
    String8 separator = S8("/");
    if (directory.length && (directory.pointer[directory.length - 1] == '/' || directory.pointer[directory.length - 1] == '\\'))
    {
        separator = S8("");
    }

    String8 parts[] = {directory, separator, file};
    return string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), true);
}
#endif

BUSTER_GLOBAL_LOCAL void font_candidate_append(String8* candidates, u64* count, String8 path)
{
    if (path.pointer && path.length)
    {
        BUSTER_CHECK(*count < BUSTER_FONT_CANDIDATE_CAPACITY);
        candidates[*count] = path;
        *count += 1;
    }
}

#if BUSTER_WINDOWS || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void font_candidate_append_file(Arena* arena, String8* candidates, u64* count, String8 directory, String8 file)
{
    font_candidate_append(candidates, count, font_path_join(arena, directory, file));
}
#endif

BUSTER_GLOBAL_LOCAL String8 font_select_first_usable(String8* candidates, u64 count, u64 first_candidate)
{
    String8 result = {0};
    for (u64 i = first_candidate; i < count; i += 1)
    {
        if (font_path_is_usable(candidates[i]))
        {
            result = string_duplicate_arena(os_state.arena, candidates[i], true);
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void font_print_attempted_paths(String8* candidates, u64 count)
{
    string_print(S8("No usable monospace font was found. Attempted locations:\n"));
    if (!count)
    {
        string_print(S8("  (none)\n"));
    }
    else
    {
        for (u64 i = 0; i < count; i += 1)
        {
            string_print(S8("  {S8}\n"), candidates[i]);
        }
    }
}

String8 font_file_get_path(FontIndex index)
{
    BUSTER_GLOBAL_LOCAL String8 table[(u64)FONT_INDEX_COUNT] = {0};

    BUSTER_CHECK(os_state.arena != 0);
    BUSTER_CHECK((u64)index < (u64)FONT_INDEX_COUNT);

    if (!font_config_initialized)
    {
        // Discovery runs once and every later call reads the resolved table
        // with a plain load, so it has to happen while the process is still
        // serial -- font_provider_prewarm() is the way to force that.
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        font_config_initialized = true;
        TemporalArena temp = scratch_begin(0, 0);
        String8 candidates[BUSTER_FONT_CANDIDATE_CAPACITY] = {0};
        u64 candidate_count = 0;

#if BUSTER_USE_FONTCONFIG
        font_candidate_append(candidates, &candidate_count, S8("fontconfig: Fira Code (Regular)"));
        if (FcInit())
        {
            FcPattern* pat = FcPatternCreate();
            if (pat)
            {
                const char* family = "Fira Code";
                const char* style = "Regular";
                FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family);
                // Try to request "Regular" but allow fontconfig to substitute.
                if (style && style[0])
                {
                    FcPatternAddString(pat, FC_STYLE, (const FcChar8*)style);
                }

                FcConfigSubstitute(NULL, pat, FcMatchPattern);
                FcDefaultSubstitute(pat);

                FcResult result = FcResultNoMatch;
                FcPattern* match = FcFontMatch(NULL, pat, &result);
                if (match)
                {
                    FcChar8* file = NULL;
                    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file)
                    {
                        String8 path = string_from_pointer((char8*)file);
                        // Keep the attempted path valid after FcFini() so a
                        // failed probe can still be reported below.
                        font_candidate_append(candidates, &candidate_count, string_duplicate_arena(temp.arena, path, true));
                    }
                    FcPatternDestroy(match);
                }

                FcPatternDestroy(pat);
            }
            FcFini();
        }
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, 1);
#elif BUSTER_WINDOWS
        String8 windir = os_get_environment_variable(S8("WINDIR"));
        if (!windir.length)
        {
            windir = S8("C:/Windows");
        }
        String8 system_fonts = font_path_join(temp.arena, windir, S8("Fonts"));
        String8 local_app_data = os_get_environment_variable(S8("LOCALAPPDATA"));
        String8 user_fonts = {0};
        if (local_app_data.length)
        {
            user_fonts = font_path_join(temp.arena, local_app_data, S8("Microsoft/Windows/Fonts"));
        }

        font_candidate_append_file(temp.arena, candidates, &candidate_count, system_fonts, S8("FiraCode-Regular.ttf"));
        if (user_fonts.length)
        {
            font_candidate_append_file(temp.arena, candidates, &candidate_count, user_fonts, S8("FiraCode-Regular.ttf"));
        }

        // These are installed with common Windows releases and provide a
        // usable monospace fallback when FiraCode is not installed.
        String8 fallback_files[] = {
            S8("CascadiaMono.ttf"),
            S8("CascadiaCode.ttf"),
            S8("consola.ttf"),
            S8("lucon.ttf"),
            S8("cour.ttf"),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(fallback_files); i += 1)
        {
            font_candidate_append_file(temp.arena, candidates, &candidate_count, system_fonts, fallback_files[i]);
        }
        if (user_fonts.length)
        {
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(fallback_files); i += 1)
            {
                font_candidate_append_file(temp.arena, candidates, &candidate_count, user_fonts, fallback_files[i]);
            }
        }
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, 0);
#elif BUSTER_IOS
        // Bundled into the app's Resources by CMake; resolved via file_read's
        // bundle-path lookup when an application chooses to package this font.
        font_candidate_append(candidates, &candidate_count, S8("FiraCode-Regular.ttf"));
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, 0);
#elif BUSTER_MACOS
        font_candidate_append_file(temp.arena, candidates, &candidate_count, S8("/Library/Fonts"), S8("FiraCode-Regular.ttf"));

        String8 home = os_get_environment_variable(S8("HOME"));
        if (home.length)
        {
            String8 user_fonts = font_path_join(temp.arena, home, S8("Library/Fonts"));
            font_candidate_append_file(temp.arena, candidates, &candidate_count, user_fonts, S8("FiraCode-Regular.ttf"));
        }

        String8 system_fonts = S8("/System/Library/Fonts");
        font_candidate_append_file(temp.arena, candidates, &candidate_count, system_fonts, S8("SFNSMono.ttf"));
        font_candidate_append_file(temp.arena, candidates, &candidate_count, system_fonts, S8("Menlo.ttc"));
        font_candidate_append_file(temp.arena, candidates, &candidate_count, system_fonts, S8("Monaco.ttf"));
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, 0);
#elif BUSTER_ANDROID
        font_candidate_append(candidates, &candidate_count, S8("/system/fonts/DroidSansMono.ttf"));
        font_candidate_append(candidates, &candidate_count, S8("/system/fonts/RobotoMono-Regular.ttf"));
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, 0);
#else
        scratch_end(temp);
        return (String8){0};
#endif

        if (!table[(u64)FONT_INDEX_MONO].pointer)
        {
            font_print_attempted_paths(candidates, candidate_count);
            os_fail_message(S8("no usable monospace font was found"));
        }

        scratch_end(temp);
    }

    BUSTER_CHECK(font_config_initialized);
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(table) == (u64)FONT_INDEX_COUNT);

    return table[(u64)index];
}

// Resolves the system font paths on the calling thread, so a later query from
// a gang reads a table nobody is still writing.
void font_provider_prewarm(void)
{
    (void)font_file_get_path(FONT_INDEX_MONO);
}

#define USE_STB_TRUETYPE 0

#if USE_STB_TRUETYPE

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define stbtt_uint8 u8
#define stbtt_uint16 u16
#define stbtt_uint32 u32
#define stbtt_int8 s8
#define stbtt_int16 s16
#define stbtt_int32 s32

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#define STBTT_ifloor(x) ((int)floor_f64(x))
#define STBTT_iceil(x) ((int)ceil_f64(x))
#define STBTT_sqrt(x) sqrt_f64(x)
#define STBTT_pow(x, y) pow_f64(x, y)
#define STBTT_fmod(x, y) fmod_f64(x, y)
#define STBTT_cos(x) cos_f64(x)
#define STBTT_acos(x) acos_f64(x)
#define STBTT_fabs(x) fabs_f64(x)
#define STBTT_malloc(x, u) ((void)(u), malloc(x))
#define STBTT_free(x, u) ((void)(u), free(x))
#define STBTT_assert(x) BUSTER_CHECK(x)
#define STBTT_strlen(x) strlen(x)
#define STBTT_memcpy memcpy
#define STBTT_memset memset

#include <stb/stb_truetype.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    FontTextureAtlasDescription result = {0};

    if (!create.font_path.pointer)
    {
        os_fail();
    }

    ByteSlice font_file = file_read(arena, create.font_path, (FileReadOptions){0});
    stbtt_fontinfo font_info;
    if (!stbtt_InitFont(&font_info, font_file.pointer, stbtt_GetFontOffsetForIndex(font_file.pointer, 0)))
    {
        os_fail();
    }

    u32 character_count = 256;
    result.characters = arena_allocate(arena, FontCharacter, character_count);
    result.kerning_tables = arena_allocate(arena, s32, character_count * character_count);
    result.height = (u32)sqrt_f32((f32)(create.text_height * create.text_height * character_count));
    result.width = result.height;
    result.pointer = arena_allocate(arena, u32, result.width * result.height);
    f32 scale_factor = stbtt_ScaleForPixelHeight(&font_info, (f32)create.text_height);

    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

    result.ascent = (int)round_f32((f32)ascent * scale_factor);
    result.descent = (int)round_f32((f32)descent * scale_factor);
    result.line_gap = (int)round_f32((f32)line_gap * scale_factor);

    u32 x = 0;
    u32 y = 0;
    u32 max_row_height = 0;
    u32 first_character = ' ';
    u32 last_character = '~';

    for (u32 i = first_character; i <= last_character; i += 1)
    {
        u32 width = 0;
        u32 height = 0;
        int advance = 0;
        int left_bearing = 0;

        u32 ch = i;
        FontCharacter* character = &result.characters[i];
        stbtt_GetCodepointHMetrics(&font_info, (int)ch, &advance, &left_bearing);

        character->advance = (u32)round_f32((float)advance * scale_factor);
        character->left_bearing = (u32)round_f32((float)left_bearing * scale_factor);

        u8* bitmap = stbtt_GetCodepointBitmap(&font_info, 0.0f, scale_factor, (int)ch, (int*)&width, (int*)&height, &character->x_offset, &character->y_offset);
        s32* kerning_table = result.kerning_tables + i * character_count;
        for (u32 j = first_character; j <= last_character; j += 1)
        {
            int kerning_advance = stbtt_GetCodepointKernAdvance(&font_info, (int)i, (int)j);
            kerning_table[j] = (s32)round_f32((float)kerning_advance * scale_factor);
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

        // The atlas is sized to fit the full glyph set with headroom, but only the horizontal axis
        // wraps; if a glyph doesn't fit vertically either, fail loudly instead of writing past result.pointer.
        BUSTER_CHECK(y + height <= result.height);
        BUSTER_CHECK(x + width <= result.width);

        u8* source = bitmap;
        u32* destination = result.pointer;

        for (u32 bitmap_y = 0; bitmap_y < height; bitmap_y += 1)
        {
            for (u32 bitmap_x = 0; bitmap_x < width; bitmap_x += 1)
            {
                u32 source_index = bitmap_y * width + bitmap_x;
                u32 destination_index = (bitmap_y + y) * result.width + (bitmap_x + x);
                u32 value = source[source_index];
                destination[destination_index] = ((u32)value << 24) | 0xffffff;
            }
        }

        x += width;

        stbtt_FreeBitmap(bitmap, 0);
    }

    return result;
}
#else
#include <buster/lib/truetype.h>

FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    FontTextureAtlasDescription result = {0};

    if (!create.font_path.pointer)
    {
        os_fail();
    }

    string_print(S8("Font path: {S8}\n"), create.font_path);
    ByteSlice font_file = file_read(arena, create.font_path, (FileReadOptions){0});
    string_print(S8("Font. Pointer: {u64:x}. Length: {u64}\n"), font_file.pointer, font_file.length);
    if (font_file.pointer)
    {
        TTF_FontInitialization font_initialization = truetype_font_initialize(font_file, 0);
        TTF_FontInformation font_information = font_initialization.information;

        if (font_initialization.result == TTF_FONT_INITIALIZATION_SUCCESS)
        {
            u32 character_count = UINT8_MAX + 1u;
            result.characters = arena_allocate(arena, FontCharacter, character_count);
            result.kerning_tables = arena_allocate(arena, s32, (u64)character_count * (u64)character_count);
            // Only ' '..'~' get filled below, but the renderer indexes these
            // tables with arbitrary bytes; zero the rest so unknown bytes
            // render as empty glyphs instead of reading stale arena memory.
            memset(result.characters, 0, sizeof(*result.characters) * character_count);
            memset(result.kerning_tables, 0, sizeof(*result.kerning_tables) * (u64)character_count * (u64)character_count);
            result.height = (u32)sqrt_f32((f32)(create.text_height * create.text_height * character_count));
            result.width = result.height;
            result.pointer = arena_allocate(arena, u32, (u64)result.width * (u64)result.height);
            f32 scale_factor = truetype_scale_for_pixel_height(&font_information, (f32)create.text_height);

            TTF_VerticalMetrics vertical_metrics = truetype_get_font_vertical_metrics(&font_information);

            result.ascent = (s32)round_f32((f32)vertical_metrics.ascent * scale_factor);
            result.descent = (s32)round_f32((f32)vertical_metrics.descent * scale_factor);
            result.line_gap = (s32)round_f32((f32)vertical_metrics.line_gap * scale_factor);

            u32 x = 0;
            u32 y = 0;
            u32 max_row_height = 0;
            u32 first_character = ' ';
            u32 last_character = '~';

            u64 loop_start_position = arena->position;

            for (u32 i = first_character; i <= last_character; i += 1)
            {
                u32 ch = i;
                FontCharacter* character = &result.characters[i];
                TTF_HorizontalMetrics horizontal_metrics = truetype_get_codepoint_horizontal_metrics(&font_information, ch);

                character->advance = (u32)round_f32((f32)horizontal_metrics.advance_width * scale_factor);
                character->left_bearing = (u32)round_f32((f32)horizontal_metrics.left_side_bearing * scale_factor);

                TTF_Bitmap bitmap = truetype_get_codepoint_bitmap(arena, &font_information, scale_factor, scale_factor, ch);

                s32* kerning_table = result.kerning_tables + (u64)i * character_count;
                for (u32 j = first_character; j <= last_character; j += 1)
                {
                    s32 kerning_advance = truetype_get_codepoint_kern_advance(&font_information, i, j);
                    kerning_table[j] = (s32)round_f32((f32)kerning_advance * scale_factor);
                }

                if ((x + (u32)bitmap.width) > result.width)
                {
                    y += max_row_height;
                    max_row_height = (u32)bitmap.height;
                    x = 0;
                }
                else
                {
                    max_row_height = BUSTER_MAX((u32)bitmap.height, max_row_height);
                }

                character->x = x;
                character->y = y;
                character->width = (u32)bitmap.width;
                character->height = (u32)bitmap.height;
                character->x_offset = bitmap.x_offset;
                character->y_offset = bitmap.y_offset;

                // The atlas is sized to fit the full glyph set with headroom, but only the horizontal axis
                // wraps; if a glyph doesn't fit vertically either, fail loudly instead of writing past result.pointer.
                BUSTER_CHECK(y + (u32)bitmap.height <= result.height);
                BUSTER_CHECK(x + (u32)bitmap.width <= result.width);

                for (u32 bitmap_y = 0; bitmap_y < (u32)bitmap.height; bitmap_y += 1)
                {
                    for (u32 bitmap_x = 0; bitmap_x < (u32)bitmap.width; bitmap_x += 1)
                    {
                        u64 source_index = (u64)bitmap_y * (u64)(u32)bitmap.width + bitmap_x;
                        u64 destination_index = (u64)(bitmap_y + y) * (u64)result.width + (bitmap_x + x);
                        u32 value = bitmap.pixels[source_index];
                        result.pointer[destination_index] = (value << 24u) | 0x00ffffffu;
                    }
                }

                x += (u32)bitmap.width;

                arena->position = loop_start_position;
            }
        }
        else
        {
            os_fail();
        }
    }
    else
    {
        os_fail();
    }

    return result;
}
#endif
