// System font discovery for the retained UI stack: resolves platform font
// paths (fontconfig on Linux, the system font folders on Windows/macOS,
// bundled resources on iOS) into files truetype.c can parse. A candidate is
// selected only when truetype_font_select_first_usable accepts it, so a font
// the rasterizer cannot read never hides a later candidate. Resolved paths are
// a lazily built global; font_provider_prewarm fills them serially before
// parallel readers (AGENTS.md).
//
// Map: font_fontconfig_match (Linux discovery of one face),
// font_select_first_usable (probe + keep the chosen path),
// font_print_attempted_paths (failure report), font_file_get_path (entry),
// font_texture_atlas_create (atlas through truetype_font_atlas_build).

#include <buster/lib/font_provider.h>
#include <buster/lib/file.h>
#include <buster/lib/arena.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/float.h>
#include <buster/lib/string.h>
#include <buster/lib/truetype.h>

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
#define FC_SPACING "spacing"
#define FC_WEIGHT "weight"
#define FC_MONO 100
#define FC_WEIGHT_REGULAR 80
extern FcBool FcInit(void);
extern void FcFini(void);
extern FcPattern* FcPatternCreate(void);
extern void FcPatternDestroy(FcPattern* p);
extern FcBool FcPatternAddString(FcPattern* p, const char* object, const FcChar8* s);
extern FcBool FcPatternAddInteger(FcPattern* p, const char* object, int i);
extern FcBool FcConfigSubstitute(FcConfig* config, FcPattern* p, FcMatchKind kind);
extern void FcDefaultSubstitute(FcPattern* pattern);
extern FcPattern* FcFontMatch(FcConfig* config, FcPattern* p, FcResult* result);
typedef struct _FcFontSet
{
    int nfont;
    int sfont;
    FcPattern** fonts;
} FcFontSet;
extern FcFontSet* FcFontSort(FcConfig* config, FcPattern* p, FcBool trim, void* csp, FcResult* result);
extern void FcFontSetDestroy(FcFontSet* s);
extern FcResult FcPatternGetString(const FcPattern* p, const char* object, int n, FcChar8** s);
extern FcResult FcPatternGetInteger(const FcPattern* p, const char* object, int n, int* i);
#elif BUSTER_USE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

// Configurations with nowhere to look (Linux without fontconfig) have no
// candidates to append or probe.
#if BUSTER_USE_FONTCONFIG || BUSTER_WINDOWS || BUSTER_IOS || BUSTER_MACOS || BUSTER_ANDROID
#define BUSTER_FONT_HAS_SEARCH 1
#else
#define BUSTER_FONT_HAS_SEARCH 0
#endif

#define BUSTER_FONT_CANDIDATE_CAPACITY 32u
#define BUSTER_FONT_NOTE_CAPACITY 4u

BUSTER_GLOBAL_LOCAL bool font_config_initialized = false;

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

#if BUSTER_FONT_HAS_SEARCH
BUSTER_GLOBAL_LOCAL void font_candidate_append(String8* candidates, u64* count, String8 path)
{
    if (path.pointer && path.length)
    {
        BUSTER_CHECK(*count < BUSTER_FONT_CANDIDATE_CAPACITY);
        candidates[*count] = path;
        *count += 1;
    }
}
#endif

#if BUSTER_WINDOWS || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void font_candidate_append_file(Arena* arena, String8* candidates, u64* count, String8 directory, String8 file)
{
    font_candidate_append(candidates, count, font_path_join(arena, directory, file));
}
#endif

#if BUSTER_FONT_HAS_SEARCH
// Probes the candidates with the TrueType parser the atlas builder uses and
// keeps a copy of the first accepted path. statuses records every outcome for
// font_print_attempted_paths.
BUSTER_GLOBAL_LOCAL String8 font_select_first_usable(const String8* candidates, u64 count, TTF_FontCandidateStatus* statuses)
{
    String8 result = {0};
    u64 index = truetype_font_select_first_usable(candidates, count, statuses);
    if (index < count)
    {
        result = string_duplicate_arena(os_state.arena, candidates[index], true);
    }
    return result;
}
#endif

// Reports to stderr every location that was considered and why it was not
// used. notes describe fontconfig answers that were rejected before probing.
BUSTER_GLOBAL_LOCAL void font_print_attempted_paths(const String8* candidates, const TTF_FontCandidateStatus* statuses, u64 count, const String8* notes,
                                                    u64 note_count)
{
    string_print_error(S8("No usable monospace font was found. Attempted locations:\n"));
    if (!count && !note_count)
    {
        string_print_error(S8("  (none)\n"));
    }

    for (u64 i = 0; i < count; i += 1)
    {
        string_print_error(S8("  {S8}: {S8}\n"), candidates[i], truetype_font_candidate_status_description(statuses[i]));
    }

    for (u64 i = 0; i < note_count; i += 1)
    {
        string_print_error(S8("  {S8}\n"), notes[i]);
    }
}

#if BUSTER_USE_FONTCONFIG
// Asks fontconfig for faces ordered by how well they match family (and style
// when given) and returns the first one acceptable within the first
// scan_limit entries (0 scans all): a face whose own family list contains
// family, or one the font itself marks FC_MONO. The font's own properties are
// read from the sorted set rather than from FcFontMatch, because FcFontMatch
// fills properties the font lacks from the request, so a proportional font
// would appear to satisfy FC_SPACING=FC_MONO. require_mono adds that request
// so monospace faces sort first. path is empty when nothing was acceptable and
// is allocated from arena, outliving FcFini(); rejected_path names the best
// match when it was rejected, for the failure report. accept_family also
// accepts a face by family name alone (Fira Code need not report FC_SPACING).
typedef struct FontConfigMatch FontConfigMatch;
struct FontConfigMatch
{
    String8 path;
    String8 rejected_path;
};

BUSTER_GLOBAL_LOCAL FontConfigMatch font_fontconfig_match(Arena* arena, const char* family, const char* style, bool require_mono, bool accept_family,
                                                          int scan_limit)
{
    FontConfigMatch result = {0};
    FcPattern* pattern = FcPatternCreate();
    if (pattern)
    {
        FcPatternAddString(pattern, FC_FAMILY, (const FcChar8*)family);
        if (style)
        {
            FcPatternAddString(pattern, FC_STYLE, (const FcChar8*)style);
        }
        else
        {
            FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
        }

        if (require_mono)
        {
            FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
        }

        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult sort_result = FcResultNoMatch;
        FcFontSet* fonts = FcFontSort(NULL, pattern, 1, NULL, &sort_result);
        if (fonts)
        {
            String8 wanted = string_from_pointer((char8*)family);
            int limit = scan_limit > 0 && scan_limit < fonts->nfont ? scan_limit : fonts->nfont;
            for (int font_index = 0; font_index < limit && !result.path.length; font_index += 1)
            {
                FcPattern* font = fonts->fonts[font_index];
                FcChar8* file = NULL;
                if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch && file)
                {
                    String8 path = string_from_pointer((char8*)file);
                    int spacing = 0;
                    bool acceptable = FcPatternGetInteger(font, FC_SPACING, 0, &spacing) == FcResultMatch && spacing == FC_MONO;
                    FcChar8* font_family = NULL;
                    for (int family_index = 0; accept_family && !acceptable && FcPatternGetString(font, FC_FAMILY, family_index, &font_family) == FcResultMatch && font_family;
                         family_index += 1)
                    {
                        acceptable = string_equal(string_from_pointer((char8*)font_family), wanted);
                    }

                    if (acceptable)
                    {
                        result.path = string_duplicate_arena(arena, path, true);
                    }
                    else if (!result.rejected_path.length)
                    {
                        result.rejected_path = string_duplicate_arena(arena, path, true);
                    }
                }
            }
            FcFontSetDestroy(fonts);
        }

        FcPatternDestroy(pattern);
    }
    return result;
}
#endif

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
        TTF_FontCandidateStatus statuses[BUSTER_FONT_CANDIDATE_CAPACITY] = {0};
        String8 notes[BUSTER_FONT_NOTE_CAPACITY] = {0};
        u64 note_count = 0;

#if BUSTER_USE_FONTCONFIG
        // FcFontMatch answers with the nearest installed font even when
        // nothing resembles the request (a proportional sans without Fira
        // Code), so each answer is checked: Fira Code must come back as Fira
        // Code, and the fallback must be a face fontconfig marks monospace.
        if (FcInit())
        {
            // Only the best answer counts for Fira Code: a later entry would
            // be some other font.
            FontConfigMatch fira = font_fontconfig_match(temp.arena, "Fira Code", "Regular", false, true, 1);
            if (fira.path.length)
            {
                font_candidate_append(candidates, &candidate_count, fira.path);
            }
            else
            {
                notes[note_count] = fira.rejected_path.length
                                        ? string_format(temp.arena, S8("fontconfig: Fira Code (Regular) resolved to {S8}, which is not Fira Code or monospace"), fira.rejected_path)
                                        : S8("fontconfig: Fira Code (Regular) matched no font");
                note_count += 1;
            }

            // The monospace alias is whatever the user's fontconfig maps it
            // to; take the best face that is actually marked monospace.
            FontConfigMatch mono = font_fontconfig_match(temp.arena, "monospace", 0, true, false, 0);
            if (mono.path.length)
            {
                if (!candidate_count || !string_equal(candidates[candidate_count - 1], mono.path))
                {
                    font_candidate_append(candidates, &candidate_count, mono.path);
                }
            }
            else
            {
                notes[note_count] = S8("fontconfig: no installed font marked monospace matches the monospace alias");
                note_count += 1;
            }
            FcFini();
        }
        else
        {
            notes[note_count] = S8("fontconfig: initialization failed");
            note_count += 1;
        }
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, statuses);
        bool searched = true;
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
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, statuses);
        bool searched = true;
#elif BUSTER_IOS
        // Bundled into the app's Resources by CMake; resolved via file_read's
        // bundle-path lookup when an application chooses to package this font.
        font_candidate_append(candidates, &candidate_count, S8("FiraCode-Regular.ttf"));
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, statuses);
        bool searched = true;
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
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, statuses);
        bool searched = true;
#elif BUSTER_ANDROID
        font_candidate_append(candidates, &candidate_count, S8("/system/fonts/DroidSansMono.ttf"));
        font_candidate_append(candidates, &candidate_count, S8("/system/fonts/RobotoMono-Regular.ttf"));
        table[(u64)FONT_INDEX_MONO] = font_select_first_usable(candidates, candidate_count, statuses);
        bool searched = true;
#else
        // Nowhere to look on this configuration. The table stays empty and the
        // caller reads a zero path, which is not the same as searching and
        // finding nothing -- that still fails the process below.
        bool searched = false;
#endif

        if (searched && !table[(u64)FONT_INDEX_MONO].pointer)
        {
            font_print_attempted_paths(candidates, statuses, candidate_count, notes, note_count);
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

FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    FontTextureAtlasDescription result = {0};

    if (!create.font_path.pointer)
    {
        os_fail();
    }

    ByteSlice font_file = file_read(arena, create.font_path, (FileReadOptions){0});
    if (font_file.pointer)
    {
        // truetype_font_atlas_build bounds every glyph against the atlas in
        // every build; a failed build is reported here, never as a partial atlas.
        TTF_AtlasBuild build = truetype_font_atlas_build(arena, font_file, create.text_height);
        if (build.status == TTF_ATLAS_SUCCESS)
        {
            result = build.description;
        }
        else if (build.status == TTF_ATLAS_INVALID_TEXT_HEIGHT)
        {
            os_fail_message_format(S8("font text height {u32} is outside 1..{u32}"), create.text_height, (u32)BUSTER_TTF_ATLAS_MAX_TEXT_HEIGHT);
        }
        else if (build.status == TTF_ATLAS_INVALID_FONT)
        {
            os_fail_message_format(S8("font file {S8} is not a usable TrueType font (initialization result {u32})"), create.font_path,
                                   (u32)build.initialization);
        }
        else
        {
            os_fail_message_format(S8("font file {S8} has a glyph that does not fit the atlas for text height {u32}"), create.font_path,
                                   create.text_height);
        }
    }
    else
    {
        os_fail_message_format(S8("font file {S8} could not be read"), create.font_path);
    }

    return result;
}
